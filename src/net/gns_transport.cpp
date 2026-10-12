#include "ribterra/net_transport.hpp"

#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ribterra::net {
namespace {

constexpr std::size_t hard_message_limit = 256U * 1024U;
constexpr int close_reason = 1000;

std::string result_text(const char* action, EResult result) {
    std::ostringstream out;
    out << action << " failed (GNS result " << static_cast<int>(result) << ')';
    return out.str();
}

DisconnectCause disconnect_cause(int reason) {
    return reason == k_ESteamNetConnectionEnd_Remote_Timeout ||
                   reason == k_ESteamNetConnectionEnd_Misc_Timeout
               ? DisconnectCause::Timeout
               : DisconnectCause::None;
}

} // namespace

struct NetTransport::Impl {
    struct Runtime {
        std::recursive_mutex mutex;
        std::size_t references{};
        bool initialized{};
        ISteamNetworkingSockets* sockets{};
        ISteamNetworkingUtils* utils{};
        std::unordered_map<HSteamListenSocket, Impl*> listeners;
        std::unordered_map<HSteamNetConnection, Impl*> connections;

        static Runtime& get() {
            static Runtime runtime;
            return runtime;
        }

        bool acquire(std::string& error) {
            std::scoped_lock lock(mutex);
            if (!initialized) {
                SteamDatagramErrMsg message{};
                if (!GameNetworkingSockets_Init(nullptr, message)) {
                    error = std::string("GameNetworkingSockets initialization failed: ") + message;
                    return false;
                }
                sockets = SteamNetworkingSockets();
                utils = SteamNetworkingUtils();
                if (sockets == nullptr || utils == nullptr) {
                    GameNetworkingSockets_Kill();
                    sockets = nullptr;
                    utils = nullptr;
                    error = "GameNetworkingSockets did not provide its interfaces";
                    return false;
                }
                initialized = true;
            }
            ++references;
            return true;
        }

        void release() {
            std::scoped_lock lock(mutex);
            if (references == 0 || --references != 0) return;
            listeners.clear();
            connections.clear();
            sockets = nullptr;
            utils = nullptr;
            initialized = false;
            GameNetworkingSockets_Kill();
        }
    };

    explicit Impl(TransportOptions requested) : options(std::move(requested)) {
        options.max_message_size = std::min(options.max_message_size, hard_message_limit);
        if (options.max_poll_events == 0) options.max_poll_events = 1;
        owns_runtime = Runtime::get().acquire(error);
    }

    ~Impl() {
        shutdown();
        if (owns_runtime) Runtime::get().release();
    }

    TransportOptions options;
    bool owns_runtime{};
    bool stopped{};
    HSteamListenSocket listener{k_HSteamListenSocket_Invalid};
    std::uint16_t bound_port{};
    std::unordered_set<HSteamNetConnection> connections;
    std::unordered_set<HSteamNetConnection> connected;
    std::deque<Event> pending;
    std::string error;

    static std::uint32_t public_handle(HSteamNetConnection connection) {
        return static_cast<std::uint32_t>(connection);
    }

    static HSteamNetConnection native_handle(std::uint32_t connection) {
        return static_cast<HSteamNetConnection>(connection);
    }

    static void status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
        if (info == nullptr) return;
        auto& runtime = Runtime::get();
        std::scoped_lock lock(runtime.mutex);

        Impl* owner = nullptr;
        if (const auto connection_owner = runtime.connections.find(info->m_hConn);
            connection_owner != runtime.connections.end()) {
            owner = connection_owner->second;
        } else if (const auto listener_owner = runtime.listeners.find(info->m_info.m_hListenSocket);
                   listener_owner != runtime.listeners.end()) {
            owner = listener_owner->second;
        }
        if (owner == nullptr || owner->stopped) return;
        owner->on_status(*info);
    }

    std::array<SteamNetworkingConfigValue_t, 5> config() const {
        std::array<SteamNetworkingConfigValue_t, 5> values;
        values[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                         reinterpret_cast<void*>(status_changed));
        values[1].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize,
                           static_cast<std::int32_t>(std::max<std::size_t>(options.max_message_size, 64U)));
        values[2].SetInt32(k_ESteamNetworkingConfig_SendBufferSize,
                           static_cast<std::int32_t>(std::clamp<std::size_t>(
                               options.max_send_backlog_bytes, 4096U,
                               static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))));
        // GNS currently uses a fixed rate, not an estimated link bandwidth.
        // Its default 256 KiB/s cannot resume a fully explored height map
        // within the existing command-recovery window. Keep both clamps equal.
        const auto rate = static_cast<std::int32_t>(std::clamp<std::size_t>(
            options.send_rate_bytes_per_second, 64U * 1024U, 4U * 1024U * 1024U));
        values[3].SetInt32(k_ESteamNetworkingConfig_SendRateMin, rate);
        values[4].SetInt32(k_ESteamNetworkingConfig_SendRateMax, rate);
        return values;
    }

    bool configure_lanes(HSteamNetConnection connection) {
        constexpr std::array<int, 3> priorities{0, 1, 2};
        constexpr std::array<std::uint16_t, 3> weights{1, 1, 1};
        const auto result = Runtime::get().sockets->ConfigureConnectionLanes(
            connection, static_cast<int>(priorities.size()), priorities.data(), weights.data());
        if (result == k_EResultOK) return true;
        error = result_text("ConfigureConnectionLanes", result);
        return false;
    }

    void on_status(const SteamNetConnectionStatusChangedCallback_t& info) {
        auto& runtime = Runtime::get();
        switch (info.m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (!connections.contains(info.m_hConn)) {
                connections.insert(info.m_hConn);
                runtime.connections[info.m_hConn] = this;
                if (!configure_lanes(info.m_hConn) ||
                    runtime.sockets->AcceptConnection(info.m_hConn) != k_EResultOK) {
                    const std::string failure = error.empty() ? "failed to accept connection" : error;
                    runtime.connections.erase(info.m_hConn);
                    connections.erase(info.m_hConn);
                    runtime.sockets->CloseConnection(info.m_hConn, close_reason, failure.c_str(), false);
                    pending.push_back({Event::Type::Disconnected, public_handle(info.m_hConn), {}, failure});
                }
            }
            break;
        case k_ESteamNetworkingConnectionState_Connected:
            if (connections.contains(info.m_hConn) && connected.insert(info.m_hConn).second) {
                pending.push_back({Event::Type::Connected, public_handle(info.m_hConn), {}, {}});
            }
            break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            if (!connections.contains(info.m_hConn)) break;
            std::string reason = info.m_info.m_szEndDebug;
            if (reason.empty()) reason = "connection ended";
            runtime.connections.erase(info.m_hConn);
            connections.erase(info.m_hConn);
            connected.erase(info.m_hConn);
            runtime.sockets->CloseConnection(info.m_hConn, 0, nullptr, false);
            pending.push_back({Event::Type::Disconnected, public_handle(info.m_hConn), {},
                               std::move(reason), disconnect_cause(info.m_info.m_eEndReason)});
            break;
        }
        default:
            break;
        }
    }

    bool listen(std::string_view bind_address, std::uint16_t port) {
        auto& runtime = Runtime::get();
        std::scoped_lock lock(runtime.mutex);
        if (!owns_runtime) return false;
        error.clear();
        if (stopped) {
            error = "transport is not running";
            return false;
        }
        if (listener != k_HSteamListenSocket_Invalid) {
            error = "transport is already listening";
            return false;
        }

        SteamNetworkingIPAddr address;
        address.Clear();
        const std::string text(bind_address);
        if (!text.empty() && text != "*" && !address.ParseString(text.c_str())) {
            error = "invalid listen address: " + text;
            return false;
        }
        address.m_port = port;
        auto values = config();
        listener = runtime.sockets->CreateListenSocketIP(address, static_cast<int>(values.size()), values.data());
        if (listener == k_HSteamListenSocket_Invalid) {
            std::ostringstream out;
            out << "could not listen on " << text << ':' << port
                << "; the address is unavailable or the port is already in use";
            error = out.str();
            return false;
        }
        runtime.listeners[listener] = this;
        SteamNetworkingIPAddr actual;
        actual.Clear();
        bound_port = runtime.sockets->GetListenSocketAddress(listener, &actual) ? actual.m_port : port;
        return true;
    }

    std::uint32_t connect(std::string_view endpoint) {
        auto& runtime = Runtime::get();
        std::scoped_lock lock(runtime.mutex);
        if (!owns_runtime) return 0;
        error.clear();
        if (stopped) {
            error = "transport is not running";
            return 0;
        }

        SteamNetworkingIPAddr address;
        address.Clear();
        const std::string text(endpoint);
        if (!address.ParseString(text.c_str()) || address.m_port == 0) {
            error = "invalid endpoint (expected IP address and port): " + text;
            return 0;
        }
        auto values = config();
        const auto connection = runtime.sockets->ConnectByIPAddress(
            address, static_cast<int>(values.size()), values.data());
        if (connection == k_HSteamNetConnection_Invalid) {
            error = "GameNetworkingSockets rejected endpoint: " + text;
            return 0;
        }
        connections.insert(connection);
        runtime.connections[connection] = this;
        if (!configure_lanes(connection)) {
            runtime.connections.erase(connection);
            connections.erase(connection);
            runtime.sockets->CloseConnection(connection, close_reason, error.c_str(), false);
            return 0;
        }
        return public_handle(connection);
    }

    std::vector<Event> poll() {
        auto& runtime = Runtime::get();
        std::scoped_lock lock(runtime.mutex);
        std::vector<Event> events;
        if (!owns_runtime || stopped) return events;

        runtime.sockets->RunCallbacks();
        events.reserve(options.max_poll_events);
        while (!pending.empty() && events.size() < options.max_poll_events) {
            events.push_back(std::move(pending.front()));
            pending.pop_front();
        }

        const auto snapshot = connections;
        for (const auto connection : snapshot) {
            while (events.size() < options.max_poll_events && connections.contains(connection)) {
                SteamNetworkingMessage_t* message = nullptr;
                const int count = runtime.sockets->ReceiveMessagesOnConnection(connection, &message, 1);
                if (count <= 0) break;
                if (message == nullptr) continue;
                if (message->m_cbSize < 0 || static_cast<std::size_t>(message->m_cbSize) > options.max_message_size) {
                    message->Release();
                    close(connection, "peer message exceeded the transport limit");
                    break;
                }
                Event event{};
                event.type = Event::Type::Message;
                event.connection = public_handle(connection);
                const auto* begin = static_cast<const std::uint8_t*>(message->m_pData);
                if (message->m_cbSize > 0) event.bytes.assign(begin, begin + message->m_cbSize);
                message->Release();
                events.push_back(std::move(event));
            }
            if (events.size() == options.max_poll_events) break;
        }
        return events;
    }

    bool send(std::uint32_t public_connection, std::span<const std::uint8_t> bytes,
              Delivery delivery, Lane lane) {
        auto& runtime = Runtime::get();
        std::scoped_lock lock(runtime.mutex);
        error.clear();
        const auto connection = native_handle(public_connection);
        if (!owns_runtime || stopped || !connected.contains(connection)) {
            error = "connection is not connected";
            return false;
        }
        if (bytes.size() > options.max_message_size || bytes.size() > hard_message_limit) {
            error = "message exceeds the 256 KiB transport limit";
            return false;
        }
        if (static_cast<unsigned>(lane) > static_cast<unsigned>(Lane::Snapshot)) {
            error = "invalid message lane";
            return false;
        }

        SteamNetConnectionRealTimeStatus_t status{};
        const auto status_result = runtime.sockets->GetConnectionRealTimeStatus(connection, &status, 0, nullptr);
        if (status_result != k_EResultOK) {
            error = result_text("GetConnectionRealTimeStatus", status_result);
            return false;
        }
        const auto queued = static_cast<std::size_t>(std::max(status.m_cbPendingReliable, 0)) +
                            static_cast<std::size_t>(std::max(status.m_cbPendingUnreliable, 0));
        if (bytes.size() > options.max_send_backlog_bytes ||
            queued > options.max_send_backlog_bytes - bytes.size()) {
            error = "send backlog limit reached";
            return false;
        }

        auto* message = runtime.utils->AllocateMessage(static_cast<int>(bytes.size()));
        if (message == nullptr) {
            error = "could not allocate a GameNetworkingSockets message";
            return false;
        }
        if (!bytes.empty()) std::memcpy(message->m_pData, bytes.data(), bytes.size());
        message->m_conn = connection;
        message->m_nFlags = delivery == Delivery::Reliable ? k_nSteamNetworkingSend_Reliable
                                                           : k_nSteamNetworkingSend_Unreliable;
        message->m_idxLane = static_cast<std::uint16_t>(lane);
        int64 send_result{}; // GNS uses long long on Linux; std::int64_t may be long.
        runtime.sockets->SendMessages(1, &message, &send_result, true);
        if (send_result > 0) return true;
        error = result_text("SendMessages", static_cast<EResult>(-send_result));
        return false;
    }

    void close(HSteamNetConnection connection, std::string_view reason) {
        auto& runtime = Runtime::get();
        if (!connections.contains(connection)) return;
        std::string reason_text = reason.empty() ? "connection closed" : std::string(reason);
        constexpr std::size_t max_reason_bytes = k_cchSteamNetworkingMaxConnectionCloseReason - 1U;
        if (reason_text.size() > max_reason_bytes) reason_text.resize(max_reason_bytes);
        runtime.connections.erase(connection);
        connections.erase(connection);
        connected.erase(connection);
        runtime.sockets->CloseConnection(connection, close_reason, reason_text.c_str(), true);
        pending.push_back({Event::Type::Disconnected, public_handle(connection), {}, reason_text});
    }

    void shutdown() {
        auto& runtime = Runtime::get();
        std::scoped_lock lock(runtime.mutex);
        if (stopped) return;
        stopped = true;
        if (!owns_runtime) return;
        const auto snapshot = connections;
        for (const auto connection : snapshot) {
            runtime.connections.erase(connection);
            runtime.sockets->CloseConnection(connection, close_reason, "transport shutdown", false);
        }
        connections.clear();
        connected.clear();
        if (listener != k_HSteamListenSocket_Invalid) {
            runtime.listeners.erase(listener);
            runtime.sockets->CloseListenSocket(listener);
            listener = k_HSteamListenSocket_Invalid;
        }
        bound_port = 0;
        pending.clear();
    }
};

NetTransport::NetTransport(TransportOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
NetTransport::~NetTransport() = default;
NetTransport::NetTransport(NetTransport&&) noexcept = default;
NetTransport& NetTransport::operator=(NetTransport&&) noexcept = default;

bool NetTransport::listen() {
    return impl_ != nullptr && impl_->listen(impl_->options.bind_address, impl_->options.port);
}

bool NetTransport::listen(std::string_view address, std::uint16_t port) {
    return impl_ != nullptr && impl_->listen(address, port);
}

std::uint32_t NetTransport::connect(std::string_view endpoint) {
    return impl_ == nullptr ? 0 : impl_->connect(endpoint);
}

std::vector<Event> NetTransport::poll() {
    return impl_ == nullptr ? std::vector<Event>{} : impl_->poll();
}

bool NetTransport::send(std::uint32_t connection, std::span<const std::uint8_t> bytes,
                        Delivery delivery, Lane lane) {
    return impl_ != nullptr && impl_->send(connection, bytes, delivery, lane);
}

void NetTransport::close(std::uint32_t connection, std::string_view reason) {
    if (impl_ == nullptr) return;
    auto& runtime = Impl::Runtime::get();
    std::scoped_lock lock(runtime.mutex);
    impl_->close(Impl::native_handle(connection), reason);
}

void NetTransport::shutdown() {
    if (impl_ != nullptr) impl_->shutdown();
}

std::uint16_t NetTransport::local_port() const {
    if (impl_ == nullptr) return 0;
    auto& runtime = Impl::Runtime::get();
    std::scoped_lock lock(runtime.mutex);
    return impl_->bound_port;
}

std::string NetTransport::last_error() const {
    if (impl_ == nullptr) return "transport has been moved from";
    auto& runtime = Impl::Runtime::get();
    std::scoped_lock lock(runtime.mutex);
    return impl_->error;
}

} // namespace ribterra::net

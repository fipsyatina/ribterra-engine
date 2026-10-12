#include "ribterra/network_session.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace ribterra::net {
namespace {
int reply_rank(CommandReplyStatus status) {
    return status == CommandReplyStatus::Queued                                              ? 1
           : status == CommandReplyStatus::Applied || status == CommandReplyStatus::Rejected ? 2
                                                                                             : 0;
}
} // namespace
struct NetworkServer::Impl {
    struct Peer {
        PlayerId player{};
        Challenge challenge;
        std::chrono::steady_clock::time_point connected_at, last_snapshot_at;
        std::chrono::steady_clock::time_point last_resync_at;
        std::deque<std::chrono::steady_clock::time_point> packets_at;
        std::set<ReplyReceipt> sent_replies;
        std::uint64_t chat_sequence{}, highest_sent_revision{};
        ViewSnapshot acknowledged, sent;
        std::vector<Snapshot> outgoing;
        std::size_t next_part{};
        bool has_baseline{}, snapshot_pending{}, client_ready{}, sent_all_parts{};
    };
    HostConfig config;
    GameServer game;
    NetTransport transport;
    WorldId world_id;
    std::map<std::uint32_t, Peer> peers;
    std::chrono::steady_clock::time_point last_autosave;
    std::uint64_t sequence{};
    std::uint64_t observed_revision{}, boundary_revision{};
    std::uint32_t snapshot_cadence{};
    std::string error;
    bool stopped{};
    explicit Impl(HostConfig c)
        : config(std::move(c)), game(config.game), transport(config.transport),
          world_id(game.world().export_state().world_id), last_autosave(config.game.clock()) {
        if (config.password.size() > 256 || config.autosave_interval < std::chrono::seconds(1))
            throw std::invalid_argument("Invalid host settings");
        auto backup_path = config.save_path;
        backup_path += ".bak";
        if (!config.save_path.empty() &&
            (std::filesystem::exists(config.save_path) || std::filesystem::exists(backup_path))) {
            if (!game.load(config.save_path, error))
                throw std::runtime_error("save.load_failed|" + game.last_load().source_version() + "|" + error);
            config.save_path = game.last_load().save_path;
            world_id = game.world().export_state().world_id;
        }
    }
    bool send(std::uint32_t connection, Payload payload, Lane lane = Lane::Commands) {
        try {
            auto bytes = encode({++sequence, std::move(payload)});
            if (transport.send(connection, bytes, Delivery::Reliable, lane))
                return true;
            error = transport.last_error();
        } catch (const std::exception &e) {
            error = e.what();
        }
        return false;
    }
    void reject(std::uint32_t connection, std::string reason) {
        send(connection, Rejected{reason});
        transport.close(connection, reason);
        auto it = peers.find(connection);
        if (it != peers.end()) {
            game.disconnect(it->second.player);
            peers.erase(it);
        }
    }
    void observe_boundary() {
        if (observed_revision != game.revision())
            boundary_revision = observed_revision = game.revision();
    }
    void snapshot(std::uint32_t connection, Peer &peer, bool force_full = false) {
        if (!peer.client_ready)
            return;
        const auto now = config.game.clock();
        // GNS snapshots are reliable. Keep precisely one baseline transition
        // in flight; replacing it before ACK would invalidate delayed deltas.
        if (peer.snapshot_pending && !force_full)
            return;
        if (!force_full && peer.has_baseline && peer.acknowledged.revision == game.revision())
            return;
        auto view = game.snapshot(peer.player);
        auto message = make_snapshot(
            view, world_id, peer.has_baseline && !force_full ? &peer.acknowledged : nullptr);
        (void)connection;
        peer.outgoing = split_snapshot(message);
        peer.next_part = 0;
        peer.sent_all_parts = false;
        peer.highest_sent_revision = view.revision;
        peer.sent = std::move(view);
        peer.snapshot_pending = true;
        peer.last_snapshot_at = now;
    }
    void receive(std::uint32_t connection, const Packet &packet) {
        auto it = peers.find(connection);
        if (it == peers.end())
            return;
        auto &peer = it->second;
        if (!peer.player) {
            auto hello = std::get_if<Hello>(&packet.payload);
            if (!hello) {
                reject(connection, "Join required");
                return;
            }
            if (!config.password.empty() &&
                !constant_time_equal(hello->password_proof,
                                     password_proof(config.password, peer.challenge, *hello))) {
                reject(connection, "Invalid password proof");
                return;
            }
            auto joined = game.join(hello->name, hello->versions, hello->resume_token);
            if (!joined.accepted) {
                reject(connection, joined.reason);
                return;
            }
            peer.player = joined.player;
            send(connection,
                 Welcome{joined.player, joined.resume_token, world_id, joined.next_request});
            return;
        }
        if (!game.connected(peer.player)) {
            reject(connection, "Session revoked");
            return;
        }
        if (auto command = std::get_if<GameCommand>(&packet.payload)) {
            if (!command->control_epoch) {
                reject(connection, "Missing control session epoch");
                return;
            }
            auto reply = game.enqueue(peer.player, *command);
            if (reply.status == CommandReplyStatus::Invalid) {
                reject(connection, reply.reason);
                return;
            }
            const ReplyReceipt receipt{reply.request_id, reply.status};
            if (send(connection, reply))
                peer.sent_replies.insert(receipt);
        } else if (auto reply_ack = std::get_if<ReplyAck>(&packet.payload)) {
            game.acknowledge_replies(peer.player, reply_ack->receipts);
            for (const auto &receipt : reply_ack->receipts)
                std::erase_if(peer.sent_replies, [&](const auto &sent) {
                    if (sent.request_id != receipt.request_id)
                        return false;
                    if (receipt.status == CommandReplyStatus::Queued)
                        return sent.status == CommandReplyStatus::Queued;
                    return sent.status == CommandReplyStatus::Queued ||
                           sent.status == receipt.status;
                });
        } else if (auto ack = std::get_if<SnapshotAck>(&packet.payload)) {
            if (ack->revision > peer.highest_sent_revision) {
                reject(connection, "Impossible snapshot acknowledgement");
                return;
            }
            const bool initial = !peer.client_ready;
            peer.client_ready = true;
            if (ack->resync) {
                const auto now = config.game.clock();
                if (!initial && now - peer.last_resync_at < std::chrono::seconds(1))
                    return;
                peer.last_resync_at = now;
                if (peer.snapshot_pending &&
                    (!peer.has_baseline || ack->revision == peer.acknowledged.revision)) {
                    // Reuse the immutable transition so already queued parts and
                    // retransmitted parts belong to exactly the same assembly.
                    peer.next_part = 0;
                    peer.last_snapshot_at = now;
                } else {
                    peer.has_baseline = false;
                    peer.snapshot_pending = false;
                    snapshot(connection, peer, true);
                }
            } else if (peer.snapshot_pending && ack->revision == peer.sent.revision) {
                if (!peer.sent_all_parts) {
                    reject(connection, "Premature snapshot acknowledgement");
                    return;
                }
                peer.acknowledged = std::move(peer.sent);
                peer.has_baseline = true;
                peer.snapshot_pending = false;
                peer.outgoing.clear();
                peer.next_part = 0;
            }
        } else if (auto chat = std::get_if<ChatSend>(&packet.payload)) {
            std::string reason;
            if (!game.chat(peer.player, chat->text, reason))
                send(connection, Rejected{reason}, Lane::Chat);
        } else if (auto ping = std::get_if<Ping>(&packet.payload))
            send(connection, *ping);
        else
            reject(connection, "Unexpected client message");
    }
};
NetworkServer::NetworkServer(HostConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
NetworkServer::~NetworkServer() = default;
bool NetworkServer::listen() {
    if (impl_->transport.listen())
        return true;
    impl_->error = impl_->transport.last_error();
    return false;
}
void NetworkServer::poll() {
    auto &s = *impl_;
    if (s.stopped)
        return;
    for (auto &event : s.transport.poll()) {
        if (event.type == Event::Type::Connected) {
            if (s.peers.size() >= 16) {
                s.transport.close(event.connection, "Connection limit");
                continue;
            }
            Impl::Peer peer;
            peer.challenge = {secure_token(), !s.config.password.empty(), s.config.game.versions};
            peer.connected_at = s.config.game.clock();
            s.send(event.connection, peer.challenge);
            s.peers.emplace(event.connection, std::move(peer));
        } else if (event.type == Event::Type::Disconnected) {
            auto it = s.peers.find(event.connection);
            if (it != s.peers.end()) {
                s.game.disconnect(it->second.player);
                s.peers.erase(it);
            }
        } else {
            auto found = s.peers.find(event.connection);
            if (found == s.peers.end())
                continue;
            auto &peer = found->second;
            const auto now = s.config.game.clock();
            while (!peer.packets_at.empty() &&
                   now - peer.packets_at.front() >= std::chrono::seconds(1))
                peer.packets_at.pop_front();
            if (event.bytes.size() > (peer.player ? 16384U : 4096U) ||
                peer.packets_at.size() >= (peer.player ? 160U : 8U)) {
                s.reject(event.connection, "Incoming packet limit");
                continue;
            }
            peer.packets_at.push_back(now);
            std::string reason;
            auto packet = decode(event.bytes, reason, peer.player ? 16384 : 4096);
            if (!packet) {
                s.reject(event.connection, "Malformed packet: " + reason);
                continue;
            }
            s.receive(event.connection, *packet);
        }
    }
    s.game.pump_boundary();
    s.observe_boundary();
    std::vector<std::uint32_t> expired;
    for (auto &[connection, peer] : s.peers) {
        if (!peer.player) {
            if (s.config.game.clock() - peer.connected_at > std::chrono::seconds(10))
                expired.push_back(connection);
            continue;
        }
        if ((!peer.client_ready &&
             s.config.game.clock() - peer.connected_at > std::chrono::seconds(10)) ||
            (peer.snapshot_pending &&
             s.config.game.clock() - peer.last_snapshot_at > std::chrono::seconds(30))) {
            expired.push_back(connection);
            continue;
        }
        if (!peer.client_ready)
            continue;
        for (const auto &reply : s.game.pending_replies(peer.player)) {
            const ReplyReceipt receipt{reply.request_id, reply.status};
            if (!peer.sent_replies.contains(receipt) && s.send(connection, reply))
                peer.sent_replies.insert(receipt);
        }
        // Keep a paused boundary pending until its revision is acknowledged,
        // including when it occurred behind another snapshot already in flight.
        if (!peer.has_baseline || peer.acknowledged.revision < s.boundary_revision)
            s.snapshot(connection, peer);
        for (int sent_parts = 0; peer.next_part < peer.outgoing.size() && sent_parts < 4;
             ++sent_parts) {
            if (!s.send(connection, peer.outgoing[peer.next_part], Lane::Snapshot))
                break;
            ++peer.next_part;
            if (peer.next_part == peer.outgoing.size())
                peer.sent_all_parts = true;
        }
        for (const auto &entry : s.game.chat_since(peer.player, peer.chat_sequence)) {
            if (s.send(connection, entry, Lane::Chat))
                peer.chat_sequence = entry.sequence;
            else
                break;
        }
    }
    for (auto connection : expired)
        s.reject(connection, "Session acknowledgement timeout");
    if (!s.config.save_path.empty() &&
        s.config.game.clock() - s.last_autosave >= s.config.autosave_interval) {
        save();
        s.last_autosave = s.config.game.clock();
    }
}
void NetworkServer::tick() {
    auto &s = *impl_;
    if (s.stopped)
        return;
    s.observe_boundary();
    const int speed=s.game.simulation_speed();
    for(int i=0;i<speed;++i)s.game.tick();
    s.observed_revision = s.game.revision();
    // A cadence boundary can occur while a previous snapshot awaits its ACK.
    // Retain that request so poll sends the newest view after the ACK, even if
    // simulation pauses before another fourth tick. Fast links still use 5 Hz.
    const bool snapshot_boundary = ++s.snapshot_cadence % 4 == 0;
    if (snapshot_boundary)
        s.boundary_revision = s.game.revision();
    for (auto &[connection, peer] : s.peers)
        if (peer.player) {
            for (const auto &reply : s.game.pending_replies(peer.player)) {
                const ReplyReceipt receipt{reply.request_id, reply.status};
                if (peer.sent_replies.contains(receipt))
                    continue;
                if (!s.send(connection, reply))
                    break;
                peer.sent_replies.insert(receipt);
            }
            if (snapshot_boundary)
                s.snapshot(connection, peer);
        }
}
bool NetworkServer::save() {
    auto &s = *impl_;
    return s.config.save_path.empty() || s.game.save(s.config.save_path, s.error);
}
void NetworkServer::shutdown() {
    auto &s = *impl_;
    if (s.stopped)
        return;
    save();
    s.transport.shutdown();
    s.peers.clear();
    s.stopped = true;
}
std::uint16_t NetworkServer::port() const { return impl_->transport.local_port(); }
std::string NetworkServer::last_error() const { return impl_->error; }
std::filesystem::path NetworkServer::save_path() const { return impl_->config.save_path; }
const GameServer &NetworkServer::game() const { return impl_->game; }

struct NetworkClient::Impl {
    struct Unconfirmed {
        GameCommand command;
        std::chrono::steady_clock::time_point issued_at;
        bool replay{};
    };
    ClientConfig config;
    NetTransport transport;
    std::uint32_t connection{};
    PlayerId player{};
    WorldId world_id{};
    ViewSnapshot view;
    SnapshotAssembler assembler;
    bool ready{}, welcomed{}, has_baseline{};
    std::uint64_t sequence{}, next_request{1};
    std::vector<CommandReply> replies;
    std::map<std::uint64_t, CommandReply> received_replies;
    std::map<std::uint64_t, Unconfirmed> unconfirmed;
    std::string endpoint, session_token;
    WorldId session_world{};
    PlayerId session_player{};
    std::uint64_t session_epoch{}, last_chat_sequence{};
    bool recovery_incomplete{}, ever_ready{};
    std::optional<std::chrono::steady_clock::time_point> reconnect_until;
    std::chrono::steady_clock::time_point reconnect_at{}, replay_at{};
    std::set<ReplyReceipt> reply_acks;
    std::optional<SnapshotAck> snapshot_ack;
    std::optional<Hello> hello_pending;
    std::vector<ChatEntry> chat;
    std::string error;
    explicit Impl(ClientConfig c)
        : config(std::move(c)), transport(TransportOptions{}), assembler(config.clock) {
        if (!config.clock)
            throw std::invalid_argument("Missing client clock");
    }
    bool replay_pending() const {
        return std::any_of(unconfirmed.begin(), unconfirmed.end(),
                           [](const auto &entry) { return entry.second.replay; });
    }
    void clear_connection() {
        connection = 0;
        ready = welcomed = has_baseline = false;
        player = 0;
        hello_pending.reset();
        snapshot_ack.reset();
        reply_acks.clear();
        assembler.reset();
    }
    bool send(Payload payload, Lane lane = Lane::Commands) {
        if (!connection)
            return false;
        try {
            auto bytes = encode({++sequence, std::move(payload)});
            if (transport.send(connection, bytes, Delivery::Reliable, lane))
                return true;
            error = transport.last_error();
        } catch (const std::exception &e) {
            error = e.what();
        }
        return false;
    }
};
NetworkClient::NetworkClient(ClientConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
NetworkClient::~NetworkClient() = default;
bool NetworkClient::connect(std::string_view endpoint) {
    auto &c = *impl_;
    disconnect();
    if (!c.endpoint.empty() && c.endpoint != endpoint) {
        c.recovery_incomplete |= !c.unconfirmed.empty();
        c.unconfirmed.clear();
        c.received_replies.clear();
        c.replies.clear();
        c.chat.clear();
        c.last_chat_sequence = 0;
        c.config.resume_token.clear();
        c.session_token.clear();
        c.session_player = 0;
        c.session_epoch = 0;
        c.next_request = 1;
        c.ever_ready = false;
    }
    c.endpoint = endpoint;
    c.view = {};
    c.world_id = {};
    c.error.clear();
    c.connection = c.transport.connect(endpoint);
    if (!c.connection)
        c.error = c.transport.last_error();
    return c.connection != 0;
}
void NetworkClient::poll() {
    auto &c = *impl_;
    if (c.reconnect_until && c.config.clock() >= *c.reconnect_until) {
        disconnect();
        c.recovery_incomplete |= !c.unconfirmed.empty();
        c.unconfirmed.clear();
        c.error = "Session reconnect timed out";
    }
    if (c.reconnect_until && !c.connection && c.config.clock() >= c.reconnect_at) {
        c.view = {};
        c.world_id = {};
        c.connection = c.transport.connect(c.endpoint);
        c.reconnect_at = c.config.clock() + std::chrono::seconds(1);
    }
    for (auto &event : c.transport.poll()) {
        if (event.connection != c.connection)
            continue;
        if (event.type == Event::Type::Disconnected) {
            c.error = !c.welcomed && !c.ever_ready &&
                              event.disconnect_cause == DisconnectCause::Timeout
                          ? "network.connect_timeout"
                          : event.reason;
            c.clear_connection();
            if (c.config.auto_reconnect && c.ever_ready) {
                if (!c.reconnect_until)
                    c.reconnect_until = c.config.clock() + std::chrono::seconds(30);
                c.reconnect_at = c.config.clock() + std::chrono::seconds(1);
            }
            continue;
        }
        if (event.type != Event::Type::Message)
            continue;
        std::string reason;
        auto packet = decode(event.bytes, reason);
        if (!packet) {
            c.error = "Malformed server packet: " + reason;
            disconnect();
            return;
        }
        if (auto challenge = std::get_if<Challenge>(&packet->payload)) {
            const auto mismatch=version_mismatch(c.config.versions,challenge->versions);
            if (c.welcomed || !mismatch.empty()) {
                c.error = c.welcomed?"network.handshake_invalid":mismatch;
                disconnect();
                return;
            }
            Hello hello{c.config.name, c.config.versions, c.config.resume_token, {}};
            if (challenge->password_required)
                hello.password_proof = password_proof(c.config.password, *challenge, hello);
            c.hello_pending = std::move(hello);
        } else if (auto welcome = std::get_if<Welcome>(&packet->payload)) {
            if (c.welcomed) {
                c.error = "Repeated welcome";
                disconnect();
                return;
            }
            const bool same_session = c.session_player == welcome->player &&
                                      c.session_world == welcome->world_id &&
                                      c.session_token == welcome->resume_token;
            if (!same_session) {
                c.recovery_incomplete |= !c.unconfirmed.empty();
                c.unconfirmed.clear();
                c.received_replies.clear();
                c.replies.clear();
                c.chat.clear();
                c.last_chat_sequence = 0;
                c.next_request = welcome->next_request;
                c.session_epoch = 0;
            } else {
                for (auto &[id, pending] : c.unconfirmed) {
                    (void)id;
                    pending.replay = true;
                }
            }
            c.session_player = welcome->player;
            c.session_world = welcome->world_id;
            c.session_token = welcome->resume_token;
            c.player = welcome->player;
            c.config.resume_token = welcome->resume_token;
            c.world_id = welcome->world_id;
            c.next_request = std::max(c.next_request, welcome->next_request);
            c.welcomed = true;
            c.snapshot_ack = SnapshotAck{0, true};
        } else if (auto snapshot = std::get_if<Snapshot>(&packet->payload)) {
            if (!c.welcomed || snapshot->world_id != c.world_id) {
                c.error = "Snapshot before welcome or wrong world";
                disconnect();
                return;
            }
            // Welcome identifies the world but provides no world data. A delta
            // based on revision zero must not turn the default empty view into
            // a playable world, including after reconnecting to the same host.
            if (!c.has_baseline && !snapshot->full) {
                c.error = "Full world snapshot required";
                c.assembler.reset();
                c.snapshot_ack = SnapshotAck{0, true};
                continue;
            }
            // A replay can finish an old assembly before all repeated parts
            // arrive. Those parts acknowledge existing state; they must not
            // create a new incomplete assembly that later times out.
            if (c.ready && snapshot->revision <= c.view.revision) {
                c.snapshot_ack = SnapshotAck{snapshot->revision, false};
                continue;
            }
            const auto result =
                c.assembler.accept(c.view, c.world_id, std::move(*snapshot), reason);
            if (result == AssemblyResult::Rejected) {
                c.error = reason;
                c.ready = false;
                c.snapshot_ack = SnapshotAck{c.view.revision, true};
                continue;
            }
            if (result == AssemblyResult::Applied) {
                c.error.clear();
                c.has_baseline = true;
                c.ready = true;
                c.ever_ready = true;
                c.reconnect_until.reset();
                // A new control epoch must never resurrect an earlier session's
                // unconfirmed intent, even if a server retained its world ID.
                if (c.session_epoch && c.session_epoch != c.view.control_epoch) {
                    c.recovery_incomplete |= !c.unconfirmed.empty();
                    c.unconfirmed.clear();
                    c.received_replies.clear();
                    c.replies.clear();
                }
                c.session_epoch = c.view.control_epoch;
                c.snapshot_ack = SnapshotAck{c.view.revision, false};
            }
        } else if (auto reply = std::get_if<CommandReply>(&packet->payload)) {
            if (!c.welcomed || reply->author != c.player) {
                c.error = "Wrong command recipient";
                disconnect();
                return;
            }
            c.reply_acks.insert({reply->request_id, reply->status});
            auto prior = c.received_replies.find(reply->request_id);
            if (prior != c.received_replies.end() &&
                reply_rank(reply->status) == reply_rank(prior->second.status) &&
                *reply != prior->second) {
                c.error = "Conflicting command reply";
                disconnect();
                return;
            }
            if (prior == c.received_replies.end() ||
                reply_rank(reply->status) > reply_rank(prior->second.status)) {
                c.replies.push_back(*reply);
                c.received_replies.insert_or_assign(reply->request_id, *reply);
                if (c.replies.size() > 2048)
                    c.replies.erase(c.replies.begin());
                if (c.received_replies.size() > 2048)
                    c.received_replies.erase(c.received_replies.begin());
            }
            if (reply_rank(reply->status) == 2)
                c.unconfirmed.erase(reply->request_id);
        } else if (auto entry = std::get_if<ChatEntry>(&packet->payload)) {
            if (c.welcomed && entry->sequence > c.last_chat_sequence) {
                c.last_chat_sequence = entry->sequence;
                c.chat.push_back(*entry);
                if (c.chat.size() > 200)
                    c.chat.erase(c.chat.begin());
            }
        } else if (auto rejected = std::get_if<Rejected>(&packet->payload)) {
            c.error = rejected->reason;
            if(!c.ever_ready&&!c.config.resume_token.empty()&&rejected->reason=="Unknown session credential"){
                c.config.resume_token.clear();const auto endpoint=c.endpoint;connect(endpoint);return;
            }
        }
        else if (!std::holds_alternative<Ping>(packet->payload)) {
            c.error = "Unexpected server message";
            disconnect();
            return;
        }
    }
    if (c.assembler.expired()) {
        c.assembler.reset();
        c.ready = false;
        c.error = "Snapshot assembly timed out";
        c.snapshot_ack = SnapshotAck{c.view.revision, true};
    }
    if (c.hello_pending && c.send(*c.hello_pending))
        c.hello_pending.reset();
    if (c.snapshot_ack && c.send(*c.snapshot_ack))
        c.snapshot_ack.reset();
    if (c.ready) {
        std::erase_if(c.unconfirmed, [&](const auto &entry) {
            const auto &pending = entry.second;
            if (!pending.replay)
                return false;
            if (pending.command.control_epoch != c.view.control_epoch ||
                c.config.clock() - pending.issued_at >= std::chrono::seconds(30)) {
                c.recovery_incomplete = true;
                return true;
            }
            return false;
        });
        // Preserve request IDs and complete payloads. GameServer deduplicates
        // already accepted work. Pace missing commands below its rate limit,
        // and withhold fresh commands until ordered replay is fully submitted.
        if (c.config.clock() >= c.replay_at)
            for (auto &[id, pending] : c.unconfirmed) {
                (void)id;
                if (!pending.replay)
                    continue;
                if (c.send(pending.command)) {
                    pending.replay = false;
                    c.replay_at = c.config.clock() + std::chrono::milliseconds(50);
                }
                break;
            }
    }
    while (!c.reply_acks.empty()) {
        std::vector<ReplyReceipt> receipts;
        for (auto receipt : c.reply_acks) {
            receipts.push_back(receipt);
            if (receipts.size() == 256)
                break;
        }
        if (!c.send(ReplyAck{receipts}))
            break;
        for (auto receipt : receipts)
            c.reply_acks.erase(receipt);
    }
}
void NetworkClient::disconnect() {
    auto &c = *impl_;
    if (c.connection)
        c.transport.close(c.connection, "Client disconnected");
    c.clear_connection();
    c.reconnect_until.reset();
    for (auto &[id, pending] : c.unconfirmed) {
        (void)id;
        pending.replay = false;
    }
}
bool NetworkClient::ready() const { return impl_->ready && !impl_->replay_pending(); }
bool NetworkClient::reconnecting() const { return impl_->reconnect_until.has_value() || impl_->replay_pending(); }
bool NetworkClient::recovery_incomplete() const { return impl_->recovery_incomplete; }
PlayerId NetworkClient::player() const { return impl_->player; }
const std::string &NetworkClient::resume_token() const { return impl_->config.resume_token; }
const ViewSnapshot &NetworkClient::view() const { return impl_->view; }
const WorldId &NetworkClient::world_id() const { return impl_->world_id; }
std::uint64_t NetworkClient::command(GameCommand command) {
    auto &c = *impl_;
    if (!ready())
        return 0;
    if (c.unconfirmed.size() >= 2048 || c.next_request == UINT64_MAX) {
        c.error = "Unconfirmed command or request ID limit reached";
        return 0;
    }
    command.author = c.player;
    command.request_id = c.next_request++;
    command.issued_tick = c.view.tick;
    command.control_epoch = c.view.control_epoch;
    if (!c.send(command))
        return 0;
    c.recovery_incomplete = false;
    c.unconfirmed.emplace(command.request_id, Impl::Unconfirmed{command, c.config.clock(), false});
    return command.request_id;
}
bool NetworkClient::chat(std::string text) {
    return impl_->welcomed && impl_->send(ChatSend{std::move(text)}, Lane::Chat);
}
std::vector<CommandReply> NetworkClient::take_replies() {
    std::vector<CommandReply> out;
    out.swap(impl_->replies);
    return out;
}
std::vector<ChatEntry> NetworkClient::take_chat() {
    std::vector<ChatEntry> out;
    out.swap(impl_->chat);
    return out;
}
std::string NetworkClient::last_error() const { return impl_->error; }
} // namespace ribterra::net

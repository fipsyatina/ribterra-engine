#include "ribterra/seasons.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/building_visual.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/art_alpha.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <png.h>
#include <stdexcept>
#include <utility>

namespace ribterra {
namespace {
std::string load_error_text(Localization& localization,std::string_view error) {
    if(error == "save.climate_schema_required" || error == "save.climate_contract_invalid")
        return localization.text("save.climate_incompatible");
    if(error.starts_with("alpha.") || error == "save.copy_failed") return localization.text(error);
    const char* key="save.invalid_state";
    if(error.find("checksum")!=error.npos)key="save.checksum";
    else if(error.find("compression")!=error.npos||error.find("zstd")!=error.npos)key="save.compression";
    else if(error.find("content version")!=error.npos)key="save.content";
    else if(error.find("rules")!=error.npos||error.find("generator")!=error.npos)key="save.rules";
    else if(error.find("header")!=error.npos||error.find("format")!=error.npos)key="save.format";
    else if(error.find("length")!=error.npos||error.find("size limit")!=error.npos||error.find("truncated")!=error.npos)key="save.truncated";
    else if(error.find("open")!=error.npos||error.find("inspect")!=error.npos||error.find("read")!=error.npos)key="save.read_failed";
    else if(error.find("online session")!=error.npos)key="save.online_state";
    else if(error.find("nventory")!=error.npos||error.find("ogistics")!=error.npos||error.find("tack")!=error.npos||error.find("eservation")!=error.npos)key="save.inventory";
    return localization.text(key);
}
std::string escaped(std::string_view value) {
    std::string r;
    for (char c : value)
        switch (c) {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '\"':
            r += "&quot;";
            break;
        case '\'':
            r += "&#39;";
            break;
        default:
            r += c;
        }
    return r;
}
bool safe_path(std::string_view p) {
    if (p.empty() || p.front() == '/' || p.front() == '\\' || p.find('\\') != p.npos ||
        p.find('\0') != p.npos || (p.size() > 1 && p[1] == ':'))
        return false;
    size_t s = 0;
    while (s <= p.size()) {
        auto e = p.find('/', s);
        auto c = p.substr(s, e == p.npos ? p.size() - s : e - s);
        if (c.empty() || c == "." || c == "..")
            return false;
        if (e == p.npos)
            break;
        s = e + 1;
    }
    return true;
}
class Files final : public Rml::FileInterface {
    struct F {
        std::vector<uint8_t> b;
        size_t p{};
    };
    AssetCatalog &a;

  public:
    explicit Files(AssetCatalog &v) : a(v) {}
    Rml::FileHandle Open(const Rml::String &p) override {
        try {
            if (!safe_path(p))
                return {};
            auto *f = new F{a.read_path(p)};
            return reinterpret_cast<Rml::FileHandle>(f);
        } catch (...) {
            return {};
        }
    }
    void Close(Rml::FileHandle h) override { delete reinterpret_cast<F *>(h); }
    size_t Read(void *d, size_t n, Rml::FileHandle h) override {
        if (!h || !d)
            return 0;
        auto &f = *reinterpret_cast<F *>(h);
        auto c = std::min(n, f.b.size() - std::min(f.p, f.b.size()));
        std::copy_n(f.b.data() + f.p, c, static_cast<uint8_t *>(d));
        f.p += c;
        return c;
    }
    bool Seek(Rml::FileHandle h, long o, int w) override {
        if (!h)
            return false;
        auto &f = *reinterpret_cast<F *>(h);
        long long b = w == SEEK_SET   ? 0
                      : w == SEEK_CUR ? (long long)f.p
                      : w == SEEK_END ? (long long)f.b.size()
                                      : -1;
        if (b < 0 || b + o < 0 || (unsigned long long)(b + o) > f.b.size())
            return false;
        f.p = (size_t)(b + o);
        return true;
    }
    size_t Tell(Rml::FileHandle h) override { return h ? reinterpret_cast<F *>(h)->p : 0; }
    size_t Length(Rml::FileHandle h) override { return h ? reinterpret_cast<F *>(h)->b.size() : 0; }
};
class System final : public Rml::SystemInterface {
    render::Renderer2D &renderer;
    bool capture{};
    std::vector<std::string> errors;

  public:
    explicit System(render::Renderer2D &value) : renderer(value) {}
    double GetElapsedTime() override { return SDL_GetTicksNS() / 1e9; }
    void JoinPath(Rml::String &o, const Rml::String &d, const Rml::String &p) override {
        if (!safe_path(p)) {
            o.clear();
            return;
        }
        auto s = d.find_last_of('/');
        o = s == d.npos ? p : d.substr(0, s + 1) + p;
        if (!safe_path(o))
            o.clear();
    }
    bool LogMessage(Rml::Log::Type t, const Rml::String &m) override {
        SDL_Log("RmlUi: %s", m.c_str());
        if (capture && (t == Rml::Log::LT_ERROR || t == Rml::Log::LT_ASSERT || t == Rml::Log::LT_WARNING))
            errors.push_back(m);
        return true;
    }
    void SetClipboardText(const Rml::String &t) override { SDL_SetClipboardText(t.c_str()); }
    void GetClipboardText(Rml::String &t) override {
        char *p = SDL_GetClipboardText();
        t = p ? p : "";
        SDL_free(p);
    }
    void ActivateKeyboard(Rml::Vector2f c, float h) override {
        if (auto *w = SDL_GetKeyboardFocus()) {
            const auto top = renderer.logical_to_window({c.x, c.y});
            const auto bottom = renderer.logical_to_window({c.x, c.y + h});
            SDL_Rect r{static_cast<int>(top.x), static_cast<int>(top.y), 1,
                       std::max(1, static_cast<int>(std::abs(bottom.y - top.y)))};
            SDL_SetTextInputArea(w, &r, 0);
            SDL_StartTextInput(w);
        }
    }
    void DeactivateKeyboard() override {
        if (auto *w = SDL_GetKeyboardFocus())
            SDL_StopTextInput(w);
    }
    void begin() {
        capture = true;
        errors.clear();
    }
    std::string end() {
        capture = false;
        std::string r;
        for (auto &e : errors) {
            if (!r.empty())
                r += " | ";
            r += e;
        }
        return r;
    }
};
class Ime final : public Rml::TextInputHandler {
    Rml::TextInputContext *c{};
    int s{}, e{};

  public:
    void OnActivate(Rml::TextInputContext *v) override {
        c = v;
        s = e = 0;
    }
    void OnDeactivate(Rml::TextInputContext *v) override {
        if (c == v)
            c = nullptr;
    }
    void OnDestroy(Rml::TextInputContext *v) override { OnDeactivate(v); }
    bool focused() const { return c != nullptr; }
    void edit(const SDL_TextEditingEvent &v) {
        if (!c)
            return;
        Rml::String t = v.text ? v.text : "";
        int n = (int)Rml::StringUtilities::LengthUTF8(t);
        bool composing = s != e;
        if (!composing)
            c->GetSelectionRange(s, e);
        if (composing || n)
            c->SetText(t, s, e);
        e = s + n;
        c->SetCompositionRange(s, e);
        if (n && v.start >= 0 && v.length >= 0)
            c->SetSelectionRange(s + v.start, s + v.start + v.length);
        else if (composing)
            c->SetCursorPosition(e);
        if (composing && !n)
            c->CommitComposition({});
    }
};
struct G {
    std::vector<Rml::Vertex> v;
    std::vector<uint32_t> i;
};
struct T {
    render::TextureHandle h;
};
class FrameRender final : public Rml::RenderInterface {
    render::Renderer2D &r;
    AssetCatalog &a;
    render::Frame2D *f{};
    bool append_surface_vertices{};
    std::vector<render::Vertex> staged_vertices;
    std::vector<render::TextureHandle> retired_textures;
    void release_retired() noexcept {
        for (const auto texture : retired_textures)
            try { r.release_texture(texture); }
            catch (...) { SDL_Log("RmlUi retired texture release failed"); }
        retired_textures.clear();
    }
    bool se{};
    Rml::Rectanglei sc{};
    std::optional<Rml::Matrix4f> tr;
    [[noreturn]] static void unsupported() {
        throw std::runtime_error("RmlUi document requested an unsupported advanced rendering feature");
    }

  public:
    FrameRender(render::Renderer2D &rv, AssetCatalog &av) : r(rv), a(av) {}
    ~FrameRender() override { release_retired(); }
    void begin(render::Frame2D &v) {
        if (f)
            throw std::runtime_error("Nested UI compose");
        if (v.content != render::FrameContent::World && v.content != render::FrameContent::UiOnly)
            throw std::invalid_argument("Invalid UI frame content");
        const bool typed_prefix = v.linear_hdr_world || !v.sprite_material_draws.empty() ||
            !v.ground_material_draws.empty() || std::any_of(v.commands.begin(), v.commands.end(),
                [](const auto &command) {
                    const auto *draw = std::get_if<render::GeometryDraw>(&command);
                    return draw && (draw->surface_lit || draw->sprite_material_index || draw->ground_material_index);
                });
        const bool accompany_vertices = typed_prefix || !v.surface_vertices.empty();
        if (accompany_vertices && v.surface_vertices.size() != v.vertices.size())
            throw std::invalid_argument("UI compose requires an aligned existing surface stream");
        if (v.content == render::FrameContent::UiOnly &&
            std::any_of(v.surface_vertices.begin(), v.surface_vertices.end(), [](const auto &vertex) {
                const render::SurfaceVertex neutral{};
                return vertex.world_position != neutral.world_position || vertex.normal != neutral.normal;
            }))
            throw std::invalid_argument("UI-only surface vertices must be canonical neutral companions");
        // RmlUi can rebuild a font atlas during Render, after emitting geometry
        // that still references it. Keep it alive until that frame was submitted.
        release_retired();
        append_surface_vertices = accompany_vertices;
        f = &v;
    }
    void end() noexcept { f = nullptr; append_surface_vertices = false; staged_vertices.clear(); }
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,
                                                Rml::Span<const int> i) override {
        if (v.empty() || i.empty())
            return {};
        auto g = std::make_unique<G>();
        g->v.assign(v.begin(), v.end());
        for (int x : i) {
            if (x < 0 || (size_t)x >= v.size())
                return {};
            g->i.push_back((uint32_t)x);
        }
        return reinterpret_cast<Rml::CompiledGeometryHandle>(g.release());
    }
    void RenderGeometry(Rml::CompiledGeometryHandle h, Rml::Vector2f t, Rml::TextureHandle tx) override {
        if (!f)
            throw std::runtime_error("UI draw outside compose");
        if (!h)
            return;
        auto &g = *reinterpret_cast<G *>(h);
        if (f->vertices.size() > UINT32_MAX || f->indices.size() > UINT32_MAX ||
            g.v.size() > UINT32_MAX - f->vertices.size() || g.i.size() > UINT32_MAX - f->indices.size())
            throw std::runtime_error("UI geometry overflow");
        if (append_surface_vertices && f->surface_vertices.size() != f->vertices.size())
            throw std::invalid_argument("UI surface stream changed during compose");
        auto fv = (uint32_t)f->vertices.size(), fi = (uint32_t)f->indices.size();
        // Validate the entire transform before appending any frame geometry.
        // Vector growth during commit retains its usual amortized behavior.
        auto &vertices = staged_vertices;
        vertices.clear();
        vertices.reserve(g.v.size());
        const bool software = r.device_info().software;
        for (auto &v : g.v) {
            Rml::Vector4f p(v.position.x + t.x, v.position.y + t.y, 0, 1);
            if (tr)
                p = *tr * p;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.w) || std::abs(p.w) < 1e-7f)
                throw std::runtime_error("Invalid UI transform");
            const render::Point position{p.x / p.w, p.y / p.w};
            if (!std::isfinite(position.x) || !std::isfinite(position.y))
                throw std::runtime_error("Invalid projected UI transform");
            auto color=v.colour;
            // SDL's CPU renderer does not support custom premultiplied blend modes.
            // Convert the UI boundary to straight alpha for that explicit backend.
            if(software&&color.alpha){
                const auto straight=[&](unsigned value){return static_cast<Rml::byte>(std::min(255u,(value*255u+color.alpha/2)/color.alpha));};
                color.red=straight(color.red);color.green=straight(color.green);color.blue=straight(color.blue);
            }
            vertices.push_back({position, {v.tex_coord.x, v.tex_coord.y},
                                {color.red, color.green, color.blue, color.alpha}});
        }
        render::GeometryDraw d{
            render::Pass::Ui, software?render::Blend::Alpha:render::Blend::PremultipliedAlpha, {}, fi, (uint32_t)g.i.size()};
        if (tx)
            d.texture = reinterpret_cast<T *>(tx)->h;
        d.scissor_enabled = se;
        d.scissor = {(float)sc.Left(), (float)sc.Top(), (float)sc.Width(), (float)sc.Height()};
        const auto surface_size = f->surface_vertices.size();
        const auto command_size = f->commands.size();
        try {
            f->vertices.insert(f->vertices.end(), vertices.begin(), vertices.end());
            if (append_surface_vertices)
                f->surface_vertices.resize(f->vertices.size());
            for (auto x : g.i)
                f->indices.push_back(fv + x);
            f->commands.emplace_back(d);
        } catch (...) {
            // Retain the pre-draw World/UI prefix if an allocation fails.
            f->commands.resize(command_size);
            f->indices.resize(fi);
            f->surface_vertices.resize(surface_size);
            f->vertices.resize(fv);
            throw;
        }
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle h) override { delete reinterpret_cast<G *>(h); }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &d, const Rml::String &s) override {
        try {
            if (!safe_path(s))
                return {};
            std::string texture_path=s;
            std::array<int,4> sprite_rect{};
            bool sprite_alpha=false;
            const auto prefix=s.find("art/building-");
            if(prefix!=std::string::npos&&s.ends_with(".png")) {
                auto slug=s.substr(prefix+13,s.size()-prefix-17);
                std::string state="ready";
                if(slug.ends_with("-level2")) {
                    slug.resize(slug.size()-7);
                    state="level2";
                }
                const auto manifest=nlohmann::json::parse(a.read_path("art/world.json"));
                for(const auto&page:manifest.at("pages"))for(const auto&sprite:page.at("sprites"))
                    if(sprite.at("id")=="building."+slug+"."+state) {
                        texture_path=page.at("path").get<std::string>();
                        sprite_rect=sprite.at("rect").get<std::array<int,4>>();
                        sprite_alpha=page.value("alpha_mode","")=="magenta";
                    }
            }
            auto enc = a.read_path(texture_path);
            png_image im{};
            im.version = PNG_IMAGE_VERSION;
            if (enc.empty() || !png_image_begin_read_from_memory(&im, enc.data(), enc.size())) { std::fprintf(stderr,"UI image header failed: %s: %s\n",s.c_str(),im.message); return {}; }
            if (!im.width || !im.height || im.width > 16384 || im.height > 16384 ||
                (uint64_t)im.width * im.height > SIZE_MAX / 4) {
                png_image_free(&im);
                return {};
            }
            im.format = PNG_FORMAT_RGBA;
            std::vector<uint8_t> p((size_t)im.width * im.height * 4);
            if (!png_image_finish_read(&im, nullptr, p.data(), 0, nullptr)) {
                std::fprintf(stderr,"UI image decode failed: %s: %s\n",s.c_str(),im.message);
                png_image_free(&im);
                return {};
            }
            d = {(int)im.width, (int)im.height};
            png_image_free(&im);
            if(sprite_rect[2]>0) {
                const auto [x,y,w,h]=sprite_rect;
                if(x<0||y<0||x+w>d.x||y+h>d.y)return {};
                std::vector<uint8_t> crop(size_t(w)*h*4);
                for(int row=0;row<h;++row)std::copy_n(p.data()+(size_t(y+row)*d.x+x)*4,size_t(w)*4,crop.data()+size_t(row)*w*4);
                p=std::move(crop);d={w,h};
                if(sprite_alpha)for(size_t i=0;i<p.size();i+=4)decode_art_magenta(p.data()+i);
            }
            if(!r.device_info().software)for (size_t i = 0; i < p.size(); i += 4) {
                auto q = p[i + 3];
                p[i] = (uint8_t)((p[i] * q + 127) / 255);
                p[i + 1] = (uint8_t)((p[i + 1] * q + 127) / 255);
                p[i + 2] = (uint8_t)((p[i + 2] * q + 127) / 255);
            }
            auto t = std::make_unique<T>();
            t->h = r.create_texture({(uint32_t)d.x, (uint32_t)d.y}, p);

            return reinterpret_cast<Rml::TextureHandle>(t.release());
        } catch (const std::exception& error) {
            std::fprintf(stderr,"UI texture failed: %s: %s\n",s.c_str(),error.what());
            return {};
        }
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> s, Rml::Vector2i d) override {
        try {
            if (d.x <= 0 || d.y <= 0 || d.x > 16384 || d.y > 16384 || (uint64_t)d.x * d.y > SIZE_MAX / 4 ||
                s.size() != (uint64_t)d.x * d.y * 4)
                return {};
            auto t = std::make_unique<T>();
            std::vector<uint8_t> straight;
            const auto* pixels=reinterpret_cast<const uint8_t*>(s.data());
            if(r.device_info().software){
                straight.assign(pixels,pixels+s.size());
                for(size_t i=0;i<straight.size();i+=4)if(const auto alpha=straight[i+3])
                    for(size_t c=0;c<3;++c)straight[i+c]=static_cast<uint8_t>(std::min(255u,(unsigned(straight[i+c])*255u+alpha/2)/alpha));
                pixels=straight.data();
            }
            t->h = r.create_texture({(uint32_t)d.x, (uint32_t)d.y}, {pixels,s.size()});
            return reinterpret_cast<Rml::TextureHandle>(t.release());
        } catch (...) {
            return {};
        }
    }
    void ReleaseTexture(Rml::TextureHandle h) override {
        if (h) {
            std::unique_ptr<T> texture(reinterpret_cast<T *>(h));
            try {
                retired_textures.push_back(texture->h);
            } catch (const std::exception &error) {
                SDL_Log("RmlUi texture release failed: %s", error.what());
            } catch (...) {
                SDL_Log("RmlUi texture release failed");
            }
        }
    }
    void EnableScissorRegion(bool v) override { se = v; }
    void SetScissorRegion(Rml::Rectanglei v) override { sc = v; }
    void SetTransform(const Rml::Matrix4f *v) override {
        tr = v ? std::optional<Rml::Matrix4f>(*v) : std::nullopt;
    }
    void EnableClipMask(bool) override { unsupported(); }
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle, Rml::Vector2f) override {
        unsupported();
    }
    Rml::LayerHandle PushLayer() override { unsupported(); }
    void CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode,
                         Rml::Span<const Rml::CompiledFilterHandle>) override {
        unsupported();
    }
    void PopLayer() override { unsupported(); }
    Rml::TextureHandle SaveLayerAsTexture() override { unsupported(); }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override { unsupported(); }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String &, const Rml::Dictionary &) override {
        unsupported();
    }
    void ReleaseFilter(Rml::CompiledFilterHandle) override {}
    Rml::CompiledShaderHandle CompileShader(const Rml::String &, const Rml::Dictionary &) override {
        unsupported();
    }
    void RenderShader(Rml::CompiledShaderHandle, Rml::CompiledGeometryHandle, Rml::Vector2f,
                      Rml::TextureHandle) override {
        unsupported();
    }
    void ReleaseShader(Rml::CompiledShaderHandle) override {}
};
class Callback final : public Rml::EventListener {
    std::function<void(Rml::Event &)> f;

  public:
    explicit Callback(std::function<void(Rml::Event &)> v) : f(std::move(v)) {}
    void ProcessEvent(Rml::Event &e) override { f(e); }
};
int mods(SDL_Keymod m) {
    int r = 0;
    if (m & SDL_KMOD_SHIFT)
        r |= Rml::Input::KM_SHIFT;
    if (m & SDL_KMOD_CTRL)
        r |= Rml::Input::KM_CTRL;
    if (m & SDL_KMOD_ALT)
        r |= Rml::Input::KM_ALT;
    if (m & SDL_KMOD_GUI)
        r |= Rml::Input::KM_META;
    return r;
}
std::optional<int> button(Uint8 b) {
    if (b == SDL_BUTTON_LEFT)
        return 0;
    if (b == SDL_BUTTON_RIGHT)
        return 1;
    if (b == SDL_BUTTON_MIDDLE)
        return 2;
    return {};
}
Rml::Input::KeyIdentifier key(SDL_Keycode v) {
    using namespace Rml::Input;
    switch (v) {
    case SDLK_BACKSPACE:
        return KI_BACK;
    case SDLK_TAB:
        return KI_TAB;
    case SDLK_RETURN:
        return KI_RETURN;
    case SDLK_KP_ENTER:
        return KI_NUMPADENTER;
    case SDLK_ESCAPE:
        return KI_ESCAPE;
    case SDLK_SPACE:
        return KI_SPACE;
    case SDLK_LEFT:
        return KI_LEFT;
    case SDLK_RIGHT:
        return KI_RIGHT;
    case SDLK_UP:
        return KI_UP;
    case SDLK_DOWN:
        return KI_DOWN;
    case SDLK_DELETE:
        return KI_DELETE;
    case SDLK_HOME:
        return KI_HOME;
    case SDLK_END:
        return KI_END;
    default:
        break;
    }
    if (v >= SDLK_A && v <= SDLK_Z)
        return (KeyIdentifier)(KI_A + v - SDLK_A);
    if (v >= SDLK_0 && v <= SDLK_9)
        return (KeyIdentifier)(KI_0 + v - SDLK_0);
    return KI_UNKNOWN;
}
const char *present_mode_value(render::PresentMode mode) {
    switch (mode) {
    case render::PresentMode::VSync:
        return "vsync";
    case render::PresentMode::Immediate:
        return "immediate";
    case render::PresentMode::Mailbox:
        return "mailbox";
    }
    throw std::invalid_argument("Invalid presentation mode");
}
std::optional<render::PresentMode> parse_present_mode(std::string_view value) {
    if (value == "vsync")
        return render::PresentMode::VSync;
    if (value == "immediate")
        return render::PresentMode::Immediate;
    if (value == "mailbox")
        return render::PresentMode::Mailbox;
    return std::nullopt;
}
std::optional<render::PresentationRequest> parse_presentation(const nlohmann::json &value) {
    if (!value.is_object() || !value.contains("mode") || !value.contains("frame_limit") ||
        !value.contains("frames_in_flight") || !value.at("mode").is_string() ||
        !value.at("frame_limit").is_number_integer() ||
        !value.at("frames_in_flight").is_number_integer())
        return std::nullopt;
    try {
        const auto mode = parse_present_mode(value.at("mode").get<std::string>());
        const auto frame_limit = value.at("frame_limit").get<std::int64_t>();
        const auto frames_in_flight = value.at("frames_in_flight").get<std::int64_t>();
        if (!mode || frame_limit < 0 || frame_limit > 1000 || frames_in_flight < 1 ||
            frames_in_flight > 3)
            return std::nullopt;
        return render::PresentationRequest{*mode, static_cast<std::uint32_t>(frame_limit),
                                           static_cast<std::uint32_t>(frames_in_flight)};
    } catch (...) {
        return std::nullopt;
    }
}
void text(Rml::ElementDocument *d, const char *i, std::string_view v) {
    if (auto *e = d->GetElementById(i)) {
        const auto markup = escaped(v);
        // Unchanged HUD labels must not recreate text nodes and trigger layout
        // every frame while the simulation or another player is updating.
        if (e->GetInnerRML() != markup)
            e->SetInnerRML(markup);
    }
}
void visible(Rml::ElementDocument *d, const char *i, bool v) {
    if (auto *e = d->GetElementById(i))
        e->SetClass("hidden", !v);
}
const char *order_phase_key(OrderPhase phase) {
    switch (phase) {
    case OrderPhase::Idle:
        return "order.idle";
    case OrderPhase::PathPending:
        return "order.pending";
    case OrderPhase::Active:
        return "order.active";
    case OrderPhase::PathFailed:
        return "order.failed";
    }
    return "order.failed";
}
const char *path_failure_key(PathFailure failure) {
    switch (failure) {
    case PathFailure::None:
        return "order.failed";
    case PathFailure::Unreachable:
        return "path_failure.unreachable";
    case PathFailure::Timeout:
        return "path_failure.timeout";
    case PathFailure::TopologyChanged:
        return "path_failure.topology_changed";
    case PathFailure::LimitExceeded:
        return "path_failure.limit_exceeded";
    case PathFailure::ResidentUnavailable:
        return "path_failure.resident_unavailable";
    case PathFailure::TargetInvalid:
        return "path_failure.target_invalid";
    case PathFailure::ReservationConflict:
        return "path_failure.reservation_conflict";
    }
    return "order.failed";
}
const char *item_key(ItemKind item) {
    switch (item) {
    case ItemKind::Wood:
        return "hud.wood";
    case ItemKind::Stone:
        return "hud.stone";
    case ItemKind::Food:
        return "hud.food";
    case ItemKind::Planks: return "item.planks";
    case ItemKind::Clay: return "item.clay";
    case ItemKind::Bricks: return "item.bricks";
    case ItemKind::Grain: return "item.grain";
    case ItemKind::Vegetables: return "item.vegetables";
    case ItemKind::Fish: return "item.fish";
    case ItemKind::Meat: return "item.meat";
    case ItemKind::Flour: return "item.flour";
    case ItemKind::Tools: return "item.tools";
    case ItemKind::Firewood: return "item.firewood";
    case ItemKind::Hide: return "item.hide";
    case ItemKind::Spear: return "item.spear";

    }
    return "hud.wood";
}
const char *construction_kind_key(BuildingKind kind) { return building_definition(kind).label_key; }
const char *logistics_phase_key(LogisticsPhase phase) {
    switch (phase) {
    case LogisticsPhase::Queued:
        return "logistics.queued";
    case LogisticsPhase::ToPickup:
        return "logistics.to_pickup";
    case LogisticsPhase::ToDropoff:
        return "logistics.to_dropoff";
    case LogisticsPhase::Complete:
        return "logistics.complete";
    case LogisticsPhase::Failed:
        return "logistics.failed";
    }
    return "logistics.failed";
}
const char *logistics_failure_key(LogisticsFailure failure) {
    switch (failure) {
    case LogisticsFailure::None:
        return "logistics.complete";
    case LogisticsFailure::Unreachable:
        return "logistics_failure.unreachable";
    case LogisticsFailure::Timeout:
        return "logistics_failure.timeout";
    case LogisticsFailure::ResidentUnavailable:
        return "logistics_failure.resident_unavailable";
    case LogisticsFailure::StorageFull:
        return "logistics_failure.storage_full";
    case LogisticsFailure::Cancelled:
        return "logistics_failure.cancelled";
    case LogisticsFailure::LimitExceeded:
        return "logistics_failure.limit_exceeded";
    case LogisticsFailure::TargetInvalid:
        return "logistics_failure.target_invalid";
    }
    return "logistics.failed";
}
const char *construction_phase_key(ConstructionPhase phase) {
    switch (phase) {
    case ConstructionPhase::Planned:
        return "construction.planned";
    case ConstructionPhase::Delivery:
        return "construction.delivery";
    case ConstructionPhase::Work:
        return "construction.work";
    case ConstructionPhase::Complete:
        return "construction.complete";
    case ConstructionPhase::Cancelled:
        return "construction.cancelled";
    }
    return "construction.cancelled";
}
const char *work_status_key(const ResidentWorkView &work) {
    if (work.phase == WorkIntentPhase::Waiting)
        return "work.waiting";
    if (work.phase == WorkIntentPhase::PathPending)
        return work.kind == WorkIntentKind::BuildAssignment ? "work.planning_build" : "work.planning_gather";
    if (work.phase == WorkIntentPhase::Failed)
        return "work.failed";
    switch (work.completion) {
    case WorkCompletionReason::Exhausted:
        return "work.exhausted";
    case WorkCompletionReason::Superseded:
        return "work.superseded";
    case WorkCompletionReason::Cancelled:
        return "work.cancelled";
    case WorkCompletionReason::Started:
    case WorkCompletionReason::None:
        return "work.started";
    }
    return "work.failed";
}
} // namespace
} // namespace ribterra

namespace ribterra {
struct GameUi::Impl {
    LobbyState lobby_state; PlayerId lobby_player{}; bool showing_lobby{};
    std::string lobby_connection; bool lobby_address_copied{};
#include "game_ui_lobby.inc"
    render::Renderer2D &renderer;
    AssetCatalog &assets;
    Localization &localization;
    std::filesystem::path settings;
    nlohmann::json settings_document = nlohmann::json::object();
    std::optional<render::PresentationRequest> loaded_presentation;
    std::optional<render::PresentationRequest> pending_presentation;
    render::PresentationRequest presentation_edit{};
    bool presentation_save_failed{};
    Files files;
    System system;
    Ime ime;
    FrameRender render;
    Rml::Context *context{};
    Rml::ElementDocument *menu{}, *hud{};
    std::vector<std::unique_ptr<Callback>> listeners;
    std::vector<UiAction> actions;
    std::vector<ChatEntry> chat;
    std::string inventory_markup, delivery_markup;
    std::size_t delivery_page{}, delivery_page_count{1};
    bool chat_open{};
    bool world{}, hud_enabled{true}, initialized{}, syncing_controls{}, help_returns_to_world{};
    bool speed_allowed{true};
    bool selection_details_open{}, building_details_open{}, controls_tray_open{};
    bool action_build_allowed{}, action_selection_allowed{}, action_cancel_allowed{};
    std::string action_hint_target;
    std::string status_value;
    Uint64 status_started{};
    int selection_primary_id{};
    float scale{1};
    render::Extent logical_size{};
    static inline std::mutex mutex;
    static inline bool active{};
    Impl(render::Renderer2D &r, AssetCatalog &a, Localization &l, std::filesystem::path s,
         render::Extent size, bool restore_presentation_settings)
        : renderer(r), assets(a), localization(l), settings(std::move(s)), files(a), system(r), render(r, a) {
        logical_size = size;
        {
            std::scoped_lock lock(mutex);
            if (active)
                throw std::runtime_error("Only one RmlUi context may be active");
            active = true;
        }
        try {
            load_settings();
            presentation_edit = restore_presentation_settings && loaded_presentation
                                    ? *loaded_presentation
                                    : renderer.presentation_status().requested;
            if (restore_presentation_settings && loaded_presentation) {
                UiAction action{UiActionType::PresentationSettings};
                action.presentation = *loaded_presentation;
                pending_presentation = action.presentation;
                queue(std::move(action));
            }
            Rml::SetSystemInterface(&system);
            Rml::SetFileInterface(&files);
            Rml::SetRenderInterface(&render);
            Rml::SetTextInputHandler(&ime);
            if (!Rml::Initialise())
                throw std::runtime_error("Could not initialize RmlUi");
            initialized = true;
            if (!Rml::LoadFontFace("fonts/NotoSans-Regular.ttf", "Noto", Rml::Style::FontStyle::Normal,
                                   Rml::Style::FontWeight::Auto, false))
                throw std::runtime_error("Could not load UI font");
            if (!Rml::LoadFontFace("fonts/CormorantGaramond.ttf", "Cormorant", Rml::Style::FontStyle::Normal,
                                   Rml::Style::FontWeight::Normal, false))
                throw std::runtime_error("Could not load heading font");
            if (!Rml::LoadFontFace("fonts/PTSerif-Regular.ttf", "PTSerif", Rml::Style::FontStyle::Normal,
                                   Rml::Style::FontWeight::Normal, false))
                throw std::runtime_error("Could not load HUD serif font");
            context = Rml::CreateContext("ribterra", {(int)size.width, (int)size.height}, &render, &ime);
            if (!context)
                throw std::runtime_error("Could not create UI context");
            system.begin();
            menu = context->LoadDocument("ui/menu.rml");
            hud = context->LoadDocument("ui/hud.rml");
            auto errors = system.end();
            if (!menu || !hud || !errors.empty())
                throw std::runtime_error("Invalid UI assets: " + errors);
            validate();
            bind();
            translate();
            show_menu();
            system.begin();
            context->Update();
            errors = system.end();
            if (!errors.empty())
                throw std::runtime_error("Invalid UI assets: " + errors);
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~Impl() { shutdown(); }
    void shutdown() noexcept {
        if (context) {
            try {
                if (menu)
                    menu->Close();
                if (hud)
                    hud->Close();
                context->Update();
                Rml::RemoveContext(context->GetName());
            } catch (...) {
                SDL_Log("RmlUi context teardown failed");
            }
        }
        menu = hud = nullptr;
        context = nullptr;
        if (initialized) {
            try {
                Rml::Shutdown();
            } catch (...) {
                SDL_Log("RmlUi shutdown failed");
            }
            initialized = false;
        }
        listeners.clear();
        std::scoped_lock lock(mutex);
        active = false;
    }
    void validate() {
        const char *m[] = {"new-game", "continue-game", "open-settings", "open-help",
                           "quit-game", "menu-status", "settings-card", "language", "ui-scale",
                           "presentation-title", "present-mode-label", "present-mode", "fps-cap-label",
                           "fps-cap", "frames-in-flight-label", "frames-in-flight",
                           "presentation-requested", "presentation-effective", "presentation-explanation",
                           "presentation-reset", "presentation-apply", "settings-back", "help-card",
                           "help-back"};
        const char *h[] = {"day",
                           "wood",
                           "stone",
                           "food",
                           "population",
                            "food-warning",
                            "food-warning-title",
                            "food-warning-detail",
                            "food-warning-action",
                           "resident-health-reason",
                           "build-more",
                           "building-catalog",
                           "catalog-title",
                           "catalog-close",
                           "pause",
                           "speed-1",
                           "speed-2",
                           "speed-4",
                           "save",
                           "load",
                           "hud-help",
                           "hud-menu",
                           "chat-log",
                           "chat-input",
                           "chat-send",
                           "status",
                            "selection-panel",
                            "selection-details",
                            "selection-detail-body",
                           "selection-title",
                           "resident-name",
                           "selection-count",
                           "resident-health",
                           "resident-hunger",
                           "resident-layer",
                           "resident-order",
                           "resident-order-detail",
                           "selection-pending",
                           "resident-carry",
                           "resident-logistics",
                           "resident-logistics-failure",
                           "resident-work",
                           "resident-work-detail",
                           "stocks-toggle",
                           "stocks-panel",
                           "stocks-title",
                           "stocks-explanation",
                           "stocks-table",
                           "stocks-carrying",
                           "deliveries-title",
                           "deliveries-list",
                           "stocks-close",
                           "delivery-pages",
                           "delivery-prev",
                           "delivery-next",
                           "delivery-page",
                           "surface-tab",
                           "mine-tab",
                           "use-passage",
                           "orders-open",
                           "orders-panel",
                           "orders-title",
                           "orders-person",
                           "orders-control",
                           "orders-body",
                           "orders-hint",
                           "orders-gather-label",
                           "orders-gather",
                           "orders-policy-hint",
                           "orders-queue-title",
                           "orders-list",
                           "orders-authority",
                           "orders-player-label",
                           "orders-player",
                           "orders-control-buttons",
                           "orders-transfer",
                           "orders-release",
                           "orders-release-hint",
                            "orders-close"};
        const char *v01[] = {"resource-wood", "resource-stone", "resource-food",
                             "wood-short-label", "stone-short-label", "food-short-label",
                             "population-label", "population-short-label", "building-summary",
                             "building-summary-close", "building-summary-art", "building-summary-title",
                             "building-summary-level", "building-summary-state", "building-summary-reason",
                             "building-summary-storage", "building-summary-storage-label",
                             "building-summary-storage-value", "building-summary-storage-fill",
                             "building-summary-details", "action-strip", "action-build", "action-gather",
                             "action-assign", "action-cancel", "action-strip-hint", "minimap-hit",
                             "hud-settings-toggle", "system-controls"};
        for (auto *i : m)
            if (!menu->GetElementById(i))
                throw std::runtime_error(std::string("Missing menu element: ") + i);
        for (auto *i : h)
            if (!hud->GetElementById(i))
                throw std::runtime_error(std::string("Missing HUD element: ") + i);
        for (auto *i : v01)
            if (!hud->GetElementById(i))
                throw std::runtime_error(std::string("Missing V01 HUD element: ") + i);
    }
    void load_settings() {
        try {
            std::ifstream in(settings);
            if (!in)
                return;
            in >> settings_document;
            if (!settings_document.is_object())
                throw std::runtime_error("UI settings root must be an object");
            try {
                localization.set_locale(settings_document.value("locale", std::string("en")));
            } catch (...) {
                localization.set_locale("en");
            }
            try {
                scale = std::clamp(settings_document.value("ui_scale", 1.f), .85f, 1.3f);
            } catch (...) {
                scale = 1;
            }
            if (const auto found = settings_document.find("presentation"); found != settings_document.end())
                loaded_presentation = parse_presentation(*found);
        } catch (...) {
            settings_document = nlohmann::json::object();
            localization.set_locale("en");
            scale = 1;
        }
    }
    void save_settings() {
        if (settings.has_parent_path())
            std::filesystem::create_directories(settings.parent_path());
        settings_document["locale"] = localization.locale();
        settings_document["ui_scale"] = scale;
        std::ofstream out(settings);
        out << settings_document.dump(2) << '\n';
        out.flush();
        out.close();
        if (!out)
            throw std::runtime_error("Could not save UI settings");
        presentation_save_failed = false;
    }
    void set_menu_enabled(const char *id, bool value, std::string_view reason = {}) {
        auto *element = menu->GetElementById(id);
        if (value) {
            element->RemoveAttribute("disabled");
            element->RemoveAttribute("title");
        } else {
            element->SetAttribute("disabled", "disabled");
            if (!reason.empty())
                element->SetAttribute("title", std::string(reason));
        }
    }
    void set_select_option_enabled(const char *id, int index, bool value, std::string_view reason = {}) {
        auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById(id));
        auto *option = select ? select->GetOption(index) : nullptr;
        if (!option)
            throw std::runtime_error(std::string("Missing option ") + std::to_string(index) +
                                     " in select " + id);
        if (value) {
            option->RemoveAttribute("disabled");
            option->RemoveAttribute("title");
        } else {
            option->SetAttribute("disabled", "disabled");
            if (!reason.empty())
                option->SetAttribute("title", std::string(reason));
        }
    }
    std::string presentation_summary(render::PresentationRequest request) const {
        const auto mode_key = std::string("settings.present_mode.") + present_mode_value(request.mode);
        const auto fps = request.frame_limit ? std::to_string(request.frame_limit)
                                             : localization.text("settings.fps_uncapped");
        return localization.format("settings.presentation_summary",
                                   {{"mode", localization.text(mode_key)},
                                    {"fps", fps},
                                    {"frames", request.frames_in_flight}});
    }
    void sync_presentation_controls(render::PresentationRequest request) {
        presentation_edit = request;
        auto *mode = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("present-mode"));
        auto *cap = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("fps-cap"));
        auto *frames = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("frames-in-flight"));
        mode->SetValue(present_mode_value(request.mode));
        const bool standard_cap = request.frame_limit == 0 || request.frame_limit == 60 ||
                                  request.frame_limit == 120 || request.frame_limit == 144 ||
                                  request.frame_limit == 240;
        auto *custom = cap->GetOption(5);
        if (!custom)
            throw std::runtime_error("Missing custom FPS option");
        custom->SetClass("hidden", standard_cap);
        if (!standard_cap) {
            const auto value = std::to_string(request.frame_limit);
            custom->SetAttribute("value", value);
            custom->SetInnerRML(value);
        }
        cap->SetValue(std::to_string(request.frame_limit));
        frames->SetValue(std::to_string(request.frames_in_flight));
    }
    render::PresentationRequest presentation_from_controls() const {
        const auto *mode = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("present-mode"));
        const auto *cap = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("fps-cap"));
        const auto *frames =
            dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("frames-in-flight"));
        const auto parsed_mode = parse_present_mode(mode->GetValue());
        if (!parsed_mode)
            throw std::invalid_argument("Invalid presentation mode control value");
        const auto frame_limit = std::stoul(cap->GetValue());
        const auto frames_in_flight = std::stoul(frames->GetValue());
        if (frame_limit > 1000 || frames_in_flight < 1 || frames_in_flight > 3)
            throw std::invalid_argument("Invalid presentation control value");
        return {*parsed_mode, static_cast<std::uint32_t>(frame_limit),
                static_cast<std::uint32_t>(frames_in_flight)};
    }
    void refresh_presentation_status() {
        const auto &status = renderer.presentation_status();
        const auto requested = pending_presentation.value_or(status.requested);
        text(menu, "presentation-requested",
             localization.format("settings.presentation_requested",
                                 {{"value", presentation_summary(requested)}}));
        text(menu, "presentation-effective",
             localization.format("settings.presentation_effective",
                                 {{"value", presentation_summary(status.effective)}}));
        std::string explanation;
        if (pending_presentation) {
            explanation = localization.text("settings.presentation_pending");
        } else if (presentation_save_failed) {
            explanation = localization.text("settings.presentation_applied_not_saved");
        } else if (status.reason == render::PresentationUnavailableReason::UnsupportedMode) {
            explanation = localization.text("settings.presentation_unavailable.mode");
        } else if (status.reason == render::PresentationUnavailableReason::UnsupportedBackend ||
                   !status.available) {
            explanation = localization.text("settings.presentation_unavailable.backend");
        } else if (status.reason == render::PresentationUnavailableReason::ApplyFailed) {
            explanation = localization.format(
                "settings.presentation_unavailable.apply",
                {{"reason", status.error.empty() ? localization.text("settings.presentation_unknown_error")
                                                  : status.error}});
        } else {
            explanation = localization.text("settings.presentation_applied");
        }
        text(menu, "presentation-explanation", explanation);
        const auto unsupported = localization.text("settings.presentation_option_unsupported");
        set_select_option_enabled("present-mode", 1, status.available && status.immediate_supported,
                                  unsupported);
        set_select_option_enabled("present-mode", 2, status.available && status.mailbox_supported,
                                  unsupported);
        set_menu_enabled("frames-in-flight", status.available, unsupported);
        set_menu_enabled("presentation-apply", !pending_presentation);
    }
    void queue_presentation() {
        const auto request = presentation_from_controls();
        presentation_edit = request;
        UiAction action{UiActionType::PresentationSettings};
        action.presentation = request;
        pending_presentation = request;
        queue(std::move(action));
        refresh_presentation_status();
    }
    void reset_presentation_edit() {
        actions.erase(std::remove_if(actions.begin(), actions.end(), [](const UiAction &action) {
                          return action.type == UiActionType::PresentationSettings;
                      }),
                      actions.end());
        pending_presentation.reset();
        sync_presentation_controls(renderer.presentation_status().effective);
        refresh_presentation_status();
    }
    void presentation_applied() {
        const auto &status = renderer.presentation_status();
        const auto applied = pending_presentation && status.requested == *pending_presentation &&
                             status.effective == *pending_presentation &&
                             status.reason == render::PresentationUnavailableReason::None;
        const auto completed_request = pending_presentation;
        pending_presentation.reset();
        if (applied) {
            settings_document["presentation"] = {
                {"mode", present_mode_value(status.effective.mode)},
                {"frame_limit", status.effective.frame_limit},
                {"frames_in_flight", status.effective.frames_in_flight}};
            try {
                save_settings();
                loaded_presentation = status.effective;
            } catch (...) {
                presentation_save_failed = true;
            }
            if (completed_request && presentation_edit == *completed_request)
                sync_presentation_controls(status.effective);
        }
        refresh_presentation_status();
    }
    void listen(Rml::ElementDocument *d, const char *i, const char *t, std::function<void(Rml::Event &)> f) {
        auto c = std::make_unique<Callback>(std::move(f));
        d->GetElementById(i)->AddEventListener(t, c.get());
        listeners.push_back(std::move(c));
    }
    void queue(UiAction a) { actions.push_back(std::move(a)); }
    void enabled(const char *id, bool value, std::string_view reason = {}) {
        auto *element = hud->GetElementById(id);
        element->SetPseudoClass("disabled", !value);
        if (value) {
            element->RemoveAttribute("disabled");
            element->RemoveAttribute("title");
        } else {
            element->SetAttribute("disabled", "disabled");
            if (!reason.empty())
                element->SetAttribute("title", std::string(reason));
        }
    }
    void refresh_action_hint() {
        const auto *element = action_hint_target.empty() ? nullptr : hud->GetElementById(action_hint_target);
        const auto reason = element ? element->GetAttribute<Rml::String>("data-reason", "") : Rml::String{};
        const bool show = element && element->GetAttribute<Rml::String>("aria-disabled", "false") == "true" &&
                          !reason.empty();
        visible(hud, "action-strip-hint", show);
        text(hud, "action-strip-hint", show ? reason : "");
    }
    void action_enabled(const char *id, bool value, std::string_view reason = {}) {
        auto *element = hud->GetElementById(id);
        element->SetPseudoClass("disabled", !value);
        element->RemoveAttribute("disabled");
        if (value) {
            element->RemoveAttribute("aria-disabled");
            element->RemoveAttribute("data-reason");
            element->RemoveAttribute("title");
        } else {
            const std::string copy(reason);
            element->SetAttribute("aria-disabled", "true");
            element->SetAttribute("data-reason", copy);
            element->SetAttribute("title", copy);
        }
        if (action_hint_target == id)
            refresh_action_hint();
    }
    void open_stocks() {
        visible(hud, "profession-panel", false);
        visible(hud, "orders-panel", false);
        visible(hud, "building-panel", false);
        visible(hud, "building-research", false);
        building_details_open = false;
        visible(hud, "building-catalog", false);
        hud->GetElementById("build-more")->SetClass("active", false);
        for (const auto *id : {"build-more", "nav-civic", "nav-production", "nav-defense",
                               "nav-landscape"})
            hud->GetElementById(id)->SetClass("active", false);
        visible(hud, "stocks-panel", true);
        hud->GetElementById("stocks-toggle")->SetClass("active", true);
    }
#include "game_ui_manual.inc"
#include "game_ui_settlement.inc"
#include "game_ui_economy.inc"
#include "game_ui_buildings.inc"
#include "game_ui_professions.inc"
    void bind() {
        bind_lobby();
        // Keep keyboard-focused controls fully inside their scroll containers.
        // Focus() alone does not scroll in RmlUi; tall translated panels need it.
        for (auto *document : {menu, hud}) {
            auto focus = std::make_unique<Callback>([](Rml::Event &event) {
                auto *target = event.GetTargetElement();
                if (target && (target->GetTagName() == "button" || target->GetTagName() == "select" ||
                               target->GetTagName() == "input"))
                    target->ScrollIntoView({Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
                                            Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
            });
            document->AddEventListener("focus", focus.get(), true);
            listeners.push_back(std::move(focus));
        }
        bind_professions();
        bind_manual();
        bind_settlement();
        bind_economy();
        bind_buildings();
        listen(hud, "stocks-toggle", "click", [&](auto &) {
            const bool open = hud->GetElementById("stocks-panel")->IsClassSet("hidden");
            if (open)
                open_stocks();
            else {
                visible(hud, "stocks-panel", false);
                hud->GetElementById("stocks-toggle")->SetClass("active", false);
            }
        });
        for (const auto *id : {"resource-wood", "resource-stone", "resource-food", "food-warning-action"})
            listen(hud, id, "click", [&](auto &) { open_stocks(); });
        listen(hud, "selection-details", "click", [&](auto &) {
            if (!hud->GetElementById("selection-details")->HasAttribute("disabled")) {
                selection_details_open = !selection_details_open;
                visible(hud, "selection-detail-body", selection_details_open);
                text(hud, "selection-details",
                     localization.text(selection_details_open ? "v01.hide_details" : "v01.details"));
            }
        });
        listen(hud, "stocks-close", "click", [&](auto &) {
            visible(hud, "stocks-panel", false);
            hud->GetElementById("stocks-toggle")->SetClass("active", false);
        });
        listen(hud, "delivery-prev", "click", [&](auto &) {
            if (delivery_page)
                --delivery_page;
        });
        listen(hud, "delivery-next", "click", [&](auto &) {
            if (delivery_page + 1 < delivery_page_count)
                ++delivery_page;
        });
        listen(hud, "deliveries-list", "click", [&](auto &event) {
            for (auto *element = event.GetTargetElement();
                 element && element != hud->GetElementById("deliveries-list");
                 element = element->GetParentNode()) {
                const int id = element->template GetAttribute<int>("data-building-id", 0);
                if (id > 0) {
                    queue({UiActionType::CancelConstruction, id});
                    break;
                }
            }
        });
        listen(hud, "surface-tab", "click", [&](auto &) { queue({UiActionType::Layer, 0}); });
        listen(hud, "mine-tab", "click", [&](auto &) { queue({UiActionType::Layer, 1}); });
        listen(hud, "use-passage", "click", [&](auto &) { queue({UiActionType::Passage}); });
        listen(menu, "menu-load", "click", [&](auto &) { queue({UiActionType::Load}); });
        listen(menu, "new-game", "click", [&](auto &) { queue({UiActionType::NewGame}); });
        listen(menu, "continue-game", "click", [&](auto &) { queue({UiActionType::ContinueGame}); });
        listen(menu, "quit-game", "click", [&](auto &) { queue({UiActionType::Quit}); });
        listen(menu, "open-settings", "click", [&](auto &) {
            visible(menu, "menu-card", false);
            visible(menu, "settings-card", true);
            refresh_presentation_status();
        });
        listen(menu, "open-help", "click", [&](auto &) {
            help_returns_to_world = false;
            visible(menu, "menu-card", false);
            visible(menu, "help-card", true);
        });
        listen(menu, "settings-back", "click", [&](auto &) {
            visible(menu, "settings-card", false);
            visible(menu, "menu-card", true);
        });
        listen(menu, "help-back", "click", [&](auto &) {
            if (help_returns_to_world) {
                help_returns_to_world = false;
                show_world();
                return;
            }
            visible(menu, "help-card", false);
            visible(menu, "menu-card", true);
        });
        listen(menu, "language", "change", [&](auto &e) {
            if (syncing_controls)
                return;
            if (auto *s = dynamic_cast<Rml::ElementFormControlSelect *>(e.GetTargetElement())) {
                localization.set_locale(s->GetValue());
                save_settings();
                translate();
            }
        });
        listen(menu, "ui-scale", "change", [&](auto &e) {
            if (syncing_controls)
                return;
            if (auto *s = dynamic_cast<Rml::ElementFormControlSelect *>(e.GetTargetElement()))
                try {
                    scale = std::clamp(std::stof(s->GetValue()), .85f, 1.3f);
                    save_settings();
                    apply_scale();
                } catch (...) {
                }
        });
        for (const auto *id : {"present-mode", "fps-cap", "frames-in-flight"})
            listen(menu, id, "change", [&](auto &) {
                if (!syncing_controls)
                    presentation_edit = presentation_from_controls();
            });
        listen(menu, "presentation-apply", "click", [&](auto &) { queue_presentation(); });
        listen(menu, "presentation-reset", "click", [&](auto &) { reset_presentation_edit(); });
        for (const auto &definition : building_catalog)
            if (definition.kind != BuildingKind::Camp && alpha::buildable(definition.kind) &&
                !hud->GetElementById(std::string("catalog-") + definition.slug))
                throw std::runtime_error("Missing construction catalog entry");
        auto open_catalog = [this](int category) {
            visible(hud,"building-catalog",true);
            hud->GetElementById("build-more")->SetClass("active",true);
            visible(hud,"stocks-panel",false);
            hud->GetElementById("stocks-toggle")->SetClass("active",false);
            for(const auto &d:building_catalog) {
                if(d.kind==BuildingKind::Camp) continue;
                const bool show=alpha::buildable(d.kind)&&(category==0 ||
                    (category==1 && (d.kind==BuildingKind::House || d.kind==BuildingKind::Temple ||
                     d.kind==BuildingKind::Tavern || d.kind==BuildingKind::Cartographer ||
                     d.kind==BuildingKind::Research || d.kind==BuildingKind::Library)) ||
                    (category==2 && (d.kind==BuildingKind::Workshop || d.kind==BuildingKind::Smelter ||
                     d.kind==BuildingKind::Mine || d.kind==BuildingKind::Stockpile || d.kind==BuildingKind::Sawmill || d.kind==BuildingKind::Mill || d.kind==BuildingKind::Kitchen || d.kind==BuildingKind::ClayPit)) ||
                    (category==3 && d.kind==BuildingKind::Barracks) ||
                    (category==4 && (d.kind==BuildingKind::Farm || d.kind==BuildingKind::Fishing)));
                visible(hud,(std::string("catalog-")+d.slug).c_str(),show);
            }
        const char *tabs[]={"catalog-all","nav-civic","nav-production","nav-defense","nav-landscape"};
            for(int i=0;i<5;++i) hud->GetElementById(tabs[i])->SetClass("active",i==category);
        };
        listen(hud,"build-more","click",[this,open_catalog](auto &){
            if(hud->GetElementById("building-catalog")->IsClassSet("hidden")) open_catalog(0);
            else {
                visible(hud,"building-catalog",false); hud->GetElementById("build-more")->SetClass("active",false);
            }
        });
        const char *tabs[]={"catalog-all","nav-civic","nav-production","nav-defense","nav-landscape"};
        for(int i=0;i<5;++i) listen(hud,tabs[i],"click",[open_catalog,i](auto &){open_catalog(i);});
        listen(hud,"build-bridge","click",[&](auto &){
            visible(hud,"building-catalog",false); hud->GetElementById("build-more")->SetClass("active",false); queue({UiActionType::Build,0,BuildingKind::Bridge});
        });
        listen(hud, "catalog-close", "click", [&](auto &) { visible(hud, "building-catalog", false); hud->GetElementById("build-more")->SetClass("active",false); });
        for (const auto &definition : building_catalog) {
            if (definition.kind == BuildingKind::Camp || !alpha::buildable(definition.kind)) continue;
            listen(hud, (std::string("catalog-") + definition.slug).c_str(), "click", [&, kind = definition.kind](auto &) {
                if(!alpha::buildable(kind))return;
                queue({UiActionType::Build, 0, kind});
                visible(hud, "building-catalog", false); hud->GetElementById("build-more")->SetClass("active",false);
            });
        }
        listen(hud, "action-build", "click", [&, open_catalog](auto &) {
            if (action_build_allowed)
                open_catalog(0);
        });
        listen(hud, "action-gather", "click", [&](auto &) {
            if (action_selection_allowed)
                open_manual();
        });
        listen(hud, "action-assign", "click", [&](auto &) {
            if (action_selection_allowed)
                open_profession();
        });
        listen(hud, "action-cancel", "click", [&](auto &) {
            if (action_cancel_allowed)
                queue({UiActionType::CancelInteraction});
        });
        for (const auto *id : {"action-build", "action-gather", "action-assign", "action-cancel"}) {
            for (const auto *event : {"focus", "mouseover"})
                listen(hud, id, event, [this, id](auto &) {
                    action_hint_target = id;
                    refresh_action_hint();
                });
            for (const auto *event : {"blur", "mouseout"})
                listen(hud, id, event, [this, id](auto &) {
                    if (action_hint_target == id) {
                        action_hint_target.clear();
                        refresh_action_hint();
                    }
                });
        }
        listen(hud, "pause", "click", [&](auto &) { queue({UiActionType::Pause}); });
        for (const auto [id, value] : {std::pair{"speed-1", 1}, {"speed-2", 2}, {"speed-4", 4}})
            listen(hud, id, "click", [&, value](auto &) {
                if (speed_allowed) queue({UiActionType::Speed, value});
            });
        listen(hud, "hud-settings-toggle", "click", [&](auto &) {
            controls_tray_open = !controls_tray_open;
            visible(hud, "system-controls", controls_tray_open);
            hud->GetElementById("hud-settings-toggle")->SetClass("active", controls_tray_open);
        });
        listen(hud, "minimap-hit", "click", [&](Rml::Event &event) {
            auto *map = hud->GetElementById("minimap-hit");
            const auto origin = map->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size = map->GetBox().GetSize(Rml::BoxArea::Border);
            if (size.x <= 0 || size.y <= 0) return;
            UiAction action{UiActionType::FocusMap};
            action.x = int(std::lround(std::clamp((event.GetParameter<int>("mouse_x", int(origin.x)) - origin.x) /
                                                  size.x, 0.f, 1.f) * 10000.f));
            action.y = int(std::lround(std::clamp((event.GetParameter<int>("mouse_y", int(origin.y)) - origin.y) /
                                                  size.y, 0.f, 1.f) * 10000.f));
            queue(std::move(action));
        });
        listen(hud, "save", "click", [&](auto &) { queue({UiActionType::Save}); });
        listen(hud, "load", "click", [&](auto &) { queue({UiActionType::Load}); });
        listen(hud, "hud-menu", "click", [&](auto &) {
            queue({UiActionType::Menu});
            show_menu();
        });
        listen(hud, "hud-help", "click", [&](auto &) {
            queue({UiActionType::Menu});
            show_help();
        });
        auto send = [&](Rml::Event &) {
            auto *i = dynamic_cast<Rml::ElementFormControlInput *>(hud->GetElementById("chat-input"));
            if (i && !i->GetValue().empty())
                queue({UiActionType::Chat, 0, BuildingKind::House, i->GetValue()});
        };
        listen(hud, "chat-send", "click", send);
        listen(hud, "chat-input", "keydown", [&, send](Rml::Event &e) mutable {
            const auto key_identifier = e.GetParameter<int>("key_identifier", 0);
            if (key_identifier == Rml::Input::KI_RETURN || key_identifier == Rml::Input::KI_NUMPADENTER)
                send(e);
        });
    }
    void apply_scale() {
        context->SetDensityIndependentPixelRatio(scale);
        const bool large = scale >= 1.15f;
        hud->GetElementById("topbar")->SetClass("compact-labels", large);
        hud->GetElementById("layerbar")->SetClass("scale-large", large);
        hud->GetElementById("selection-panel")->SetClass("scale-large", large);
        hud->GetElementById("building-summary")->SetClass("scale-large", large);
        hud->GetElementById("action-strip")->SetClass("scale-large", large);
    }
    void set_logical_size(render::Extent size) {
        if (!size.width || !size.height)
            throw std::invalid_argument("UI logical size must be non-zero");
        if (logical_size.width == size.width && logical_size.height == size.height)
            return;
        logical_size = size;
        context->SetDimensions({static_cast<int>(size.width), static_cast<int>(size.height)});
    }
    void translate() {
        visible(hud,"nav-defense",false);
        visible(hud,"temperature-panel",true);
        visible(hud,"future-resources-note",false);
        translate_professions();
        translate_manual();
        translate_settlement();
        translate_economy();
        struct SyncGuard {
            bool &v;
            explicit SyncGuard(bool &value) : v(value) { v = true; }
            ~SyncGuard() { v = false; }
        } guard(syncing_controls);
        std::pair<const char *, const char *> m[] = {{"app-title", "app.title"},
                                                     {"menu-subtitle", "menu.subtitle"},
                                                     {"new-game", "menu.new"},
                                                     {"continue-game", "menu.continue"},
                                                     {"open-settings", "menu.settings"},
                                                     {"open-help", "menu.help"},
                                                     {"quit-game", "menu.quit"},
                                                     {"settings-title", "settings.title"},
                                                     {"language-label", "settings.language"},
                                                     {"scale-label", "settings.scale"},
                                                     {"presentation-title", "settings.presentation"},
                                                     {"present-mode-label", "settings.present_mode"},
                                                     {"fps-cap-label", "settings.fps_cap"},
                                                     {"frames-in-flight-label", "settings.frames_in_flight"},
                                                     {"presentation-reset", "settings.reset_active"},
                                                     {"presentation-apply", "settings.apply"},
                                                     {"settings-back", "settings.back"},
                                                     {"help-title", "help.title"},
                                                     {"help-body", "help.body"},
                                                     {"help-back", "help.back"}};
        for (auto [i, k] : m)
            text(menu, i, localization.text(k));
        auto *present_mode =
            dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("present-mode"));
        if (!present_mode || present_mode->GetNumOptions() != 3)
            throw std::runtime_error("Presentation mode option order is invalid");
        present_mode->GetOption(0)->SetInnerRML(escaped(localization.text("settings.present_mode.vsync")));
        present_mode->GetOption(1)->SetInnerRML(escaped(localization.text("settings.present_mode.immediate")));
        present_mode->GetOption(2)->SetInnerRML(escaped(localization.text("settings.present_mode.mailbox")));
        auto *fps_cap = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("fps-cap"));
        if (!fps_cap || fps_cap->GetNumOptions() != 6)
            throw std::runtime_error("FPS cap option order is invalid");
        fps_cap->GetOption(0)->SetInnerRML(escaped(localization.text("settings.fps_uncapped")));
        std::pair<const char *, const char *> h[] = {{"hud-title", "app.title"},
                                                     {"wood-label", "hud.wood"},
                                                     {"stone-label", "hud.stone"},
                                                     {"food-label", "v01.food_short"},
                                                     {"wood-short-label", "v01.wood_short"},
                                                     {"stone-short-label", "v01.stone_short"},
                                                     {"food-short-label", "v01.food_short"},
                                                     {"population-label", "v01.population"},
                                                     {"population-short-label", "v01.population_short"},
                                                     {"build-more", "building.more"},
                                                     {"catalog-title", "building.catalog"},
                                                     {"farm-help", "farm.help"},
                                                     {"catalog-close", "inventory.close"},
                                                     {"save", "v01.save_short"},
                                                     {"load", "v01.load_short"},
                                                     {"hud-help", "v01.help_short"},
                                                     {"hud-menu", "v01.menu_short"},
                                                     {"chat-title", "chat.title"},
                                                     {"chat-send", "chat.send"},
                                                     {"surface-tab", "layer.surface"},
                                                     {"mine-tab", "layer.mine"},
                                                     {"use-passage", "layer.passage"},
                                                     {"stocks-toggle", "inventory.toggle"},
                                                     {"stocks-title", "inventory.title"},
                                                     {"stocks-explanation", "inventory.explanation"},
                                                     {"stocks-close", "inventory.close"},
                                                     {"deliveries-title", "inventory.deliveries"},
                                                     {"delivery-prev", "inventory.previous"},
                                                     {"delivery-next", "inventory.next"},
                                                     {"selection-details", "v01.details"},
                                                     {"building-summary-details", "v01.details"},
                                                     {"food-warning-action", "v01.warning_action"}};
        for (auto [i, k] : h)
            text(hud, i, localization.text(k));
        text(hud, "economy-open", localization.text("v01.economy_short"));
        hud->GetElementById("hud-settings-toggle")->SetAttribute("title", localization.text("menu.settings"));
        for (const auto &[id, key] :
             std::array<std::pair<const char *, const char *>, 5>{{
                 {"economy-open", "economy.title"}, {"save", "hud.save"}, {"load", "hud.load"},
                 {"hud-help", "hud.help"}, {"hud-menu", "hud.menu"}}})
            hud->GetElementById(id)->SetAttribute("title", localization.text(key));
        const auto icon_button=[&](Rml::ElementDocument *doc,const char *id,const char *key,int icon){
            doc->GetElementById(id)->SetInnerRML("<img class='button-icon' src='art/glyph-"+
                std::to_string(icon)+".png'/><span>"+escaped(localization.text(key))+"</span>");
        };
        icon_button(menu,"continue-game","menu.continue",0);
        icon_button(menu,"new-game","menu.new",1);
        icon_button(menu,"menu-load","hud.load",2);
        icon_button(menu,"open-settings","menu.settings",4);
        icon_button(menu,"open-help","menu.help",5);
        icon_button(menu,"quit-game","menu.quit",6);
        icon_button(hud,"build-more","nav.buildings",7);
        icon_button(hud,"catalog-all","nav.buildings",7);
        hud->GetElementById("build-more")->SetAttribute("title",localization.text("nav.buildings"));
        hud->GetElementById("stocks-toggle")->SetAttribute("title",localization.text("nav.resources"));
        icon_button(hud,"stocks-toggle","nav.resources",8);
        icon_button(hud,"nav-civic","nav.civic",9);
        icon_button(hud,"nav-production","nav.production",10);
        icon_button(hud,"nav-defense","nav.defense",11);
        icon_button(hud,"nav-landscape","nav.landscape",12);
        icon_button(hud,"build-bridge","building.bridge",13);
        const auto quick_button = [&](const char *id, const char *key, const char *sprite) {
            hud->GetElementById(id)->SetInnerRML("<img sprite='" + std::string(sprite) +
                                                 "'/><span id='" + std::string(id) + "-label'>" +
                                                 escaped(localization.text(key)) + "</span>");
        };
        quick_button("action-build", "v01.build", "hud-action-build");
        quick_button("action-gather", "v01.gather", "hud-action-gather");
        quick_button("action-assign", "v01.assign", "hud-action-assign");
        quick_button("action-cancel", "v01.cancel", "hud-action-cancel");
        const auto available_title = localization.text("v01.available_tooltip");
        for (const auto *id : {"resource-wood", "resource-stone", "resource-food"})
            hud->GetElementById(id)->SetAttribute("title", available_title);
        if (selection_details_open)
            text(hud, "selection-details", localization.text("v01.hide_details"));
        for(const auto *name:{"coal","iron","copper","gold","silver","steel","crystals"}) {
            visible(hud,(std::string("resource-")+name).c_str(),false);
            text(hud,(std::string(name)+"-label").c_str(),localization.text(std::string("resource.")+name));
            hud->GetElementById(std::string("resource-")+name)->SetAttribute("title",localization.text("resource.future"));
        }
        std::string future_names;
        for(const auto *name:{"coal","iron","copper","gold","silver","steel","crystals"}) {
            if(!future_names.empty()) future_names+=", ";
            future_names+=localization.text(std::string("resource.")+name);
        }
        text(hud,"future-resources-note",future_names+". "+localization.text("resource.future"));
        text(hud,"bridge-help",localization.text("bridge.help"));
        for (const auto &definition : building_catalog) {
            if (definition.kind == BuildingKind::Camp || !alpha::buildable(definition.kind)) continue;
            const auto id = std::string("catalog-") + definition.slug;
            const auto cost = definition.cost;
            const auto price = std::to_string(cost.wood) + " " + localization.text("hud.wood") + " · " +
                               std::to_string(cost.stone) + " " + localization.text("hud.stone") +
                               (cost.food ? " · " + std::to_string(cost.food) + " " + localization.text("hud.food") : "");
            hud->GetElementById(id)->SetInnerRML("<img class='building-icon' src='art/building-" +
                std::string(definition.slug) + ".png'/><span class='building-name'>" + localization.text(definition.label_key) +
                "</span><span class='building-price'>" + price + "</span>");
        }
        if (auto *i = hud->GetElementById("chat-input"))
            i->SetAttribute("placeholder", localization.text("chat.placeholder"));
        if (auto *s = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("language")))
            s->SetValue(localization.locale());
        if (auto *s = dynamic_cast<Rml::ElementFormControlSelect *>(menu->GetElementById("ui-scale"))) {
            const char *value = scale < .925f    ? "0.85"
                                : scale < 1.075f ? "1"
                                : scale < 1.225f ? "1.15"
                                                 : "1.3";
            s->SetValue(value);
        }
        sync_presentation_controls(presentation_edit);
        refresh_presentation_status();
        apply_scale();
    }
    void update_inventory(const ClientSession &session) {
        const auto table = alpha_stock_table(session);
        if (table != inventory_markup) {
            inventory_markup = table;
            hud->GetElementById("stocks-table")->SetInnerRML(table);
        }
        text(hud,"stocks-explanation",localization.text("alpha.stock_explanation"));
        text(hud,"stocks-carrying",localization.text("alpha.carry_explanation"));
        std::string deliveries;
        const auto &sites = session.construction_sites();
        constexpr std::size_t page_size = 3;
        delivery_page_count = std::max<std::size_t>(1, (sites.size() + page_size - 1) / page_size);
        delivery_page = std::min(delivery_page, delivery_page_count - 1);
        visible(hud, "delivery-pages", delivery_page_count > 1);
        text(hud, "delivery-page",
             localization.format("inventory.page",
                                 {{"page", delivery_page + 1}, {"pages", delivery_page_count}}));
        const auto page_end = std::min(sites.size(), (delivery_page + 1) * page_size);
        for (std::size_t index = delivery_page * page_size; index < page_end; ++index) {
            const auto &site = sites[index];
            const auto id = std::to_string(site.building_id);
            deliveries += "<div class=\"delivery-entry\"><div class=\"delivery-name\" id=\"site-name-" + id +
                          "\"></div>";
            for (const auto *field : {"phase", "wood", "stone", "food", "work", "failure"})
                deliveries +=
                    "<div class=\"delivery-detail\" id=\"site-" + std::string(field) + "-" + id + "\"></div>";
            deliveries += "<button data-building-id=\"" + id + "\" id=\"cancel-site-" + id + "\">" +
                          escaped(localization.text("inventory.cancel")) + "</button></div>";
        }
        if (sites.empty())
            deliveries = escaped(localization.text("inventory.no_deliveries"));
        if (deliveries != delivery_markup) {
            delivery_markup = deliveries;
            hud->GetElementById("deliveries-list")->SetInnerRML(deliveries);
        }
        // Preserve the button element across work/delivery ticks so mouse-up and keyboard focus
        // continue to target the same building while its counters change.
        for (std::size_t index = delivery_page * page_size; index < page_end; ++index) {
            const auto &site = sites[index];
            const auto element_id = [&](const char *field) {
                return "site-" + std::string(field) + "-" + std::to_string(site.building_id);
            };
            text(hud, element_id("name").c_str(),
                 localization.format("inventory.site",
                                     {{"name", localization.text(construction_kind_key(site.kind))},
                                      {"x", site.site.x},
                                      {"y", site.site.y}}));
            text(hud, element_id("phase").c_str(), localization.text(construction_phase_key(site.phase)));
            if (site.phase == ConstructionPhase::Work) {
                const char *key =
                    site.worker_phase == ConstructionWorkerPhase::Working ? "construction.work_active"
                    : site.worker_phase == ConstructionWorkerPhase::Travelling
                        ? "construction.work_travelling"
                    : site.worker_phase == ConstructionWorkerPhase::Planning ? "construction.work_planning"
                                                                             : "construction.work_waiting";
                text(hud, element_id("phase").c_str(),
                     site.worker_phase == ConstructionWorkerPhase::Working
                         ? localization.format(key, {{"count", site.assigned_workers}})
                         : localization.text(key));
            }
            const auto material = [&](const char *field, const char *key, int delivered, int required) {
                visible(hud, element_id(field).c_str(), required > 0);
                if (required)
                    text(hud, element_id(field).c_str(),
                         localization.format("inventory.material", {{"name", localization.text(key)},
                                                                    {"delivered", delivered},
                                                                    {"required", required}}));
            };
            material("wood", "hud.wood", site.delivered.wood, site.required.wood);
            material("stone", "hud.stone", site.delivered.stone, site.required.stone);
            material("food", "hud.food", site.delivered.food, site.required.food);
            text(hud, element_id("work").c_str(),
                 localization.format("inventory.work", {{"count", std::lround(site.work * 100)}}));
            visible(hud, element_id("failure").c_str(),
                    site.failure != LogisticsFailure::None || site.worker_failure != PathFailure::None);
            if (site.failure != LogisticsFailure::None)
                text(hud, element_id("failure").c_str(),
                     localization.text(logistics_failure_key(site.failure)));
            else if (site.worker_failure != PathFailure::None)
                text(hud, element_id("failure").c_str(),
                     localization.text(path_failure_key(site.worker_failure)));
        }
    }
    void update(ClientSession *s, std::string_view status) {
        update_lobby(s);
        if (world && s) {
            auto incoming = s->take_chat();
            chat.insert(chat.end(), std::make_move_iterator(incoming.begin()),
                        std::make_move_iterator(incoming.end()));
            if (chat.size() > 200)
                chat.erase(chat.begin(), chat.begin() + (ptrdiff_t)(chat.size() - 200));
            const auto calendar = s->calendar();
            visible(hud,"season-icon",calendar.has_value());
            if (calendar) {
                const auto season = calendar->season;
                text(hud,"season",localization.text(season_label(season)));
                const int temperature = s->weather().current.temperature_deci_c;
                const auto magnitude = std::abs(temperature);
                const auto value = std::string(temperature < 0 ? "-" : "") +
                    std::to_string(magnitude / 10) + "." + std::to_string(magnitude % 10);
                text(hud,"temperature",localization.format("weather.temperature",{{"value",value}}));
                hud->GetElementById("season-icon")->SetAttribute("src",season==Season::Winter?"art/icons-cursors.17.png":season==Season::Spring?"art/icons-cursors.19.png":season==Season::Autumn?"art/glyph-8.png":"art/icons-cursors.16.png");
                text(hud,"day",localization.format("hud.day",{{"count",calendar->day}})+" · "+
                     clock_time(calendar->tick)+" · "+localization.text(calendar->night?"time.night":"time.day"));
            } else {
                const auto unknown = localization.text("weather.unknown");
                text(hud,"season",unknown);
                text(hud,"temperature",unknown);
                text(hud,"day",unknown);
            }
            text(hud, "wood", localization.format("hud.number", {{"count", s->inventory().available.wood}}));
            text(hud, "stone",
                 localization.format("hud.number", {{"count", s->inventory().available.stone}}));
            text(hud, "food", localization.format("hud.number", {{"count", s->inventory().available.food}}));
            const auto hungry = std::count_if(s->residents().begin(), s->residents().end(),
                                              [](const auto &r) { return r.health > 0 && r.hunger > 0; });
            const bool no_available_food = s->inventory().available.food == 0;
            const bool population_alive = std::any_of(s->residents().begin(),s->residents().end(),
                                                      [](const auto &r){return r.health > 0;});
            visible(hud,"food-warning",population_alive && (no_available_food || hungry > 0));
            text(hud,"food-warning-title",localization.text(no_available_food ? "v01.warning_no_food" :
                                                             "v01.warning_hungry"));
            text(hud,"food-warning-detail", no_available_food ? localization.text("health.no_food") :
                 localization.format("health.hungry_settlers",{{"count",hungry}}));
            update_inventory(*s);
            text(hud, "population",
                 localization.format("hud.number", {{"count", s->residents().size()}}));
            std::string log;
            for (auto &e : chat)
                log += "<div class=\"chat-entry\"><strong>" + escaped(e.name) + ":</strong> " +
                       escaped(e.text) + "</div>";
            hud->GetElementById("chat-log")->SetInnerRML(log);
        }
        const std::string next_status(status);
        if (next_status != status_value) {
            status_value = next_status;
            status_started = SDL_GetTicks();
        }
        const bool show_status = !status_value.empty() && SDL_GetTicks() - status_started < 5000;
        text(hud, "status", show_status ? status_value : std::string{});
        text(menu, "menu-status", show_status ? status_value : std::string{});
        visible(hud, "status", show_status);
        visible(menu, "menu-status", show_status);
        context->Update();
    }
    void close_chat() {
        chat_open=false;
        hud->GetElementById("chat-input")->Blur();
        visible(hud,"chat",false);
        context->Update();
    }
    void show_menu() {
        close_chat();
        world = false;
        hud->Hide();
        menu->Show();
        visible(menu, "online-card", false);
        visible(menu, "lobby-card", false);
        visible(menu, "settings-card", false);
        visible(menu, "help-card", false);
        visible(menu, "menu-card", true);
    }
    void show_world() {
        close_chat();
        world = true;
        hud_enabled = true;
        menu->Hide();
        hud->Show();
    }
    void show_help() {
        help_returns_to_world = world;
        show_menu();
        visible(menu, "menu-card", false);
        visible(menu, "help-card", true);
    }
    void set_view_state(const ClientSession *session, const std::vector<int> &selected_ids, bool paused,
                        int speed, WorldLayer layer, const std::vector<int>& buildings,
                        bool interaction_active) {
        if (session && session->remote() && session->lobby().enabled) {
            paused = session->lobby().paused;
            speed = session->lobby().speed;
        }
        text(hud, "pause", paused ? ">" : "II");
        hud->GetElementById("pause")->SetAttribute(
            "title", localization.text(paused ? "hud.resume" : "hud.pause"));
        hud->GetElementById("pause")->SetClass("active", paused);
        hud->GetElementById("surface-tab")->SetClass("active", layer == WorldLayer::Surface);
        hud->GetElementById("mine-tab")->SetClass("active", layer == WorldLayer::Mine);
        visible(hud, "buildbar", layer == WorldLayer::Surface);
        if (layer != WorldLayer::Surface) visible(hud, "building-catalog", false); hud->GetElementById("build-more")->SetClass("active",false);
        const bool snapshot_ready = session && session->snapshot_ready();
        speed_allowed = snapshot_ready && session->can_change_speed();
        for (const auto [id, value] : {std::pair{"speed-1", 1}, {"speed-2", 2}, {"speed-4", 4}}) {
            auto *button = hud->GetElementById(id);
            button->SetClass("active", speed == value);
            button->SetPseudoClass("disabled", !speed_allowed);
            if (speed_allowed && button->HasAttribute("disabled")) {
                button->RemoveAttribute("disabled");
                button->RemoveAttribute("title");
            } else if (!speed_allowed && !button->HasAttribute("disabled")) {
                button->SetAttribute("disabled", "disabled");
                button->SetAttribute("title", localization.text("lobby.host_only"));
            }
        }

        const Resident *first = nullptr;
        std::size_t count = 0;
        std::size_t pending_count = 0;
        std::size_t controllable_count = 0;
        if (session)
            for (const int selected_id : selected_ids) {
                const auto found =
                    std::find_if(session->residents().begin(), session->residents().end(),
                                 [&](const Resident &resident) { return resident.id == selected_id; });
                if (found != session->residents().end()) {
                    if (!first)
                        first = &*found;
                    ++count;
                    bool controllable = !session->remote() && found->nation == session->nation_id();
                    if (session->remote())
                        for (const auto &control : session->resident_controls())
                            if (control.resident_id == selected_id && control.held_by_recipient) {
                                controllable = true;
                                break;
                            }
                    if (controllable)
                        ++controllable_count;
                    if (found->order_phase == OrderPhase::PathPending)
                        ++pending_count;
                }
            }
        const int primary_id = first ? first->id : 0;
        if (primary_id != selection_primary_id) {
            selection_primary_id = primary_id;
            selection_details_open = false;
        }
        if (count != 1)
            selection_details_open = false;
        visible(hud, "selection-panel", first != nullptr);
        visible(hud, "selection-detail-body", first && selection_details_open);
        text(hud, "selection-details", localization.text(selection_details_open ? "v01.hide_details" : "v01.details"));
        enabled("selection-details", count == 1,
                count > 1 ? localization.text("v01.single_selection_required") : std::string_view{});
        update_profession(session, first, selected_ids);
        update_manual(session, first, selected_ids);
        update_settlement(session);
        update_economy(session,first);
        update_buildings(session,buildings);
        action_build_allowed = snapshot_ready && layer == WorldLayer::Surface;
        action_selection_allowed = count != 0 && controllable_count == count;
        action_cancel_allowed = interaction_active;
        const auto build_reason = !snapshot_ready ? localization.text("v01.waiting_snapshot")
                                  : layer != WorldLayer::Surface ? localization.text("v01.surface_only")
                                                                : std::string{};
        const auto selection_reason = count == 0 ? localization.text("v01.select_residents")
                                      : controllable_count != count ? localization.text("v01.not_controlled")
                                                                    : std::string{};
        action_enabled("action-build", action_build_allowed, build_reason);
        action_enabled("action-gather", action_selection_allowed, selection_reason);
        action_enabled("action-assign", action_selection_allowed, selection_reason);
        action_enabled("action-cancel", action_cancel_allowed, localization.text("v01.no_interaction"));
        refresh_action_hint();
        const auto *young=first&&session?session->settlement().youth(first->id,session->ticks()):nullptr;
        text(hud,"resident-age",young?localization.text("settlement.young")+" "+remaining_time(young->adult_tick,session->ticks()):"");
        if (!first)
            return;
        text(hud, "selection-title", localization.text("hud.selection"));
        text(hud, "resident-name", first->name);
        text(hud, "resident-layer",
             localization.text(first->layer == WorldLayer::Surface ? "layer.surface" : "layer.mine"));
        text(hud, "selection-count", localization.format("hud.selected_count", {{"count", count}}));
        text(hud, "selection-pending", localization.format("hud.pending_count", {{"count", pending_count}}));
        visible(hud, "selection-pending", pending_count > 0);
        text(hud, "resident-order",
             localization.format("hud.order",
                                 {{"state", localization.text(order_phase_key(first->order_phase))}}));
        for(const auto &profile:session->resident_professions()) {
            if(profile.resident_id!=first->id || profile.manual_override)continue;
            if(profile.wait_reason==ProfessionWaitReason::ManualOrder)
                text(hud,"resident-order",localization.text("profession.await_command"));
            else if(profile.wait_reason>=ProfessionWaitReason::Strolling)
                text(hud,"resident-order",localization.text("profession.wait."+std::to_string(int(profile.wait_reason))));
        }
        text(hud, "resident-order-detail", localization.text(path_failure_key(first->path_failure)));
        visible(hud, "resident-order-detail", first->path_failure != PathFailure::None);
        if (!manual_rows.empty() && (manual_rows.front().phase == ManualOrderPhase::Queued ||
                                     manual_rows.front().phase == ManualOrderPhase::WaitingBoundary)) {
            text(hud, "resident-order",
                 localization.format(
                     "hud.order",
                     {{"state", localization.text("manual.phase." + std::to_string(static_cast<int>(
                                                                        manual_rows.front().phase)))}}));
            visible(hud, "resident-order-detail", false);
        }
        const auto logistics =
            std::find_if(session->resident_logistics().begin(), session->resident_logistics().end(),
                         [&](const auto &value) { return value.resident_id == first->id; });
        const bool has_logistics = logistics != session->resident_logistics().end();
        text(hud, "resident-carry",
             has_logistics && logistics->quantity
                 ? localization.format("inventory.carry",
                                       {{"name", localization.text(item_key(logistics->item))},
                                        {"count", logistics->quantity},
                                        {"capacity", logistics->capacity}})
                 : localization.text("inventory.empty_hands"));
        visible(hud, "resident-logistics", has_logistics);
        visible(hud, "resident-logistics-failure",
                has_logistics && logistics->failure != LogisticsFailure::None);
        if (has_logistics) {
            text(hud, "resident-logistics", localization.text(logistics_phase_key(logistics->phase)));
            text(hud, "resident-logistics-failure",
                 localization.text(logistics_failure_key(logistics->failure)));
        }
        const auto work = std::find_if(session->resident_work().begin(), session->resident_work().end(),
                                       [&](const auto &value) { return value.resident_id == first->id; });
        const bool has_work =
            work != session->resident_work().end() && !(has_logistics && logistics->quantity > 0);
        visible(hud, "resident-work", has_work);
        visible(hud, "resident-work-detail",
                has_work && (work->phase == WorkIntentPhase::Waiting || work->failure != PathFailure::None));
        if (has_work) {
            text(hud, "resident-work", localization.text(work_status_key(*work)));
            text(hud, "resident-work-detail",
                 localization.text(work->phase == WorkIntentPhase::Waiting
                                       ? "work.retry"
                                       : path_failure_key(work->failure)));
        }
        if (first->job == Job::Farm) {
            visible(hud, "resident-work", true);
            visible(hud, "resident-work-detail", true);
            const bool working = first->progress > 0;
            text(hud, "resident-work", localization.text(working ? "farm.working" : "farm.travelling"));
            text(hud, "resident-work-detail", localization.format("farm.progress", {{"percent", int(first->progress*100)}}));
        }
        text(hud, "resident-health", localization.format("hud.health", {{"count", first->health}}));
        text(hud, "resident-hunger", localization.format("hud.hunger", {{"count", std::clamp(10.f - first->hunger, 0.f, 10.f)}}));
        const char *health_reason = first->health == 0 ? "health.dead" :
                                    first->hunger >= 5 ? "health.starving" :
                                    first->hunger > 0 ? "health.hungry" :
                                    first->health < 100 ? "health.fed_after_damage" : nullptr;
        visible(hud,"resident-health-reason",health_reason != nullptr);
        text(hud,"resident-health-reason",health_reason ? localization.text(health_reason) : "");
        if (count > 1) {
            text(hud, "resident-name", localization.format("v01.resident_group", {{"count", count}}));
            text(hud, "resident-order", localization.text("v01.group_common_actions"));
            visible(hud, "resident-health-reason", false);
        }
    }
    Rml::Element *element(std::string_view id) const {
        const std::string key(id);
        if (auto *e = menu->GetElementById(key))
            return e;
        return hud->GetElementById(key);
    }
};
GameUi::GameUi(render::Renderer2D &r, AssetCatalog &a, Localization &l, std::filesystem::path s,
               render::Extent z, bool restore_presentation_settings)
    : impl_(std::make_unique<Impl>(r, a, l, std::move(s), z, restore_presentation_settings)) {}
GameUi::~GameUi() = default;
void GameUi::set_logical_size(render::Extent size) { impl_->set_logical_size(size); }
bool GameUi::process_event(const SDL_Event &e) {
    if (impl_->world && e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1) {
        if (!e.key.repeat)
            toggle_hud();
        return true;
    }
    if (impl_->world && !impl_->hud_enabled)
        return false;
    auto *c = impl_->context;
    bool p = true;
    const bool keyboard_event = e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP;
    const auto modifiers = keyboard_event ? e.key.mod : SDL_GetModState();
    const int m = mods(modifiers);
    // Command shortcuts use physical letter keys in non-Latin layouts too.
    // Read modifiers from this event: a queued key-up may already have cleared
    // SDL's current modifier state before the key-down is processed.
    const auto key_identifier = [&] {
        if ((modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) &&
            e.key.scancode >= SDL_SCANCODE_A && e.key.scancode <= SDL_SCANCODE_Z)
            return static_cast<Rml::Input::KeyIdentifier>(static_cast<int>(Rml::Input::KI_A) +
                static_cast<int>(e.key.scancode) - static_cast<int>(SDL_SCANCODE_A));
        return key(e.key.key);
    };
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        p = c->ProcessMouseMove((int)e.motion.x, (int)e.motion.y, m);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (auto b = button(e.button.button))
            p = c->ProcessMouseButtonDown(*b, m);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (auto b = button(e.button.button))
            p = c->ProcessMouseButtonUp(*b, m);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        p = c->ProcessMouseWheel({-e.wheel.x, -e.wheel.y}, m);
        break;
    case SDL_EVENT_KEY_DOWN: {
        const bool enter=e.key.key==SDLK_RETURN || e.key.key==SDLK_KP_ENTER;
        const auto *current_focus=c->GetFocusElement();
        const bool control_focus=current_focus && current_focus->IsVisible(true) &&
            (current_focus->GetTagName()=="button" || current_focus->GetTagName()=="select" || current_focus->GetTagName()=="input" || current_focus->GetTagName()=="option");
        if(impl_->world && enter && !impl_->chat_open && !impl_->ime.focused() && !control_focus) {
            if(!e.key.repeat) {
                impl_->chat_open=true;visible(impl_->hud,"chat",true);c->Update();
                impl_->hud->GetElementById("chat-input")->Focus();
            }
            return true;
        }
        if(impl_->world && impl_->chat_open && e.key.key==SDLK_ESCAPE) {
            impl_->close_chat();return true;
        }
        if (impl_->world && e.key.key == SDLK_ESCAPE && !e.key.repeat) {
            const auto is_open = [&](const char *id) {
                const auto *element = impl_->hud->GetElementById(id);
                return element && !element->IsClassSet("hidden");
            };
            const auto close = [&](const char *id) {
                visible(impl_->hud, id, false);
                c->Update();
            };
            if (is_open("system-controls")) {
                impl_->controls_tray_open = false;
                close("system-controls");
                impl_->hud->GetElementById("hud-settings-toggle")->SetClass("active", false);
                impl_->hud->GetElementById("hud-settings-toggle")->Focus();
                return true;
            }
            if (is_open("building-research")) {
                close("building-research");
                return true;
            }
            if (is_open("building-panel")) {
                impl_->building_details_open = false;
                close("building-panel");
                close("building-research");
                impl_->hud->GetElementById("building-summary-details")->Focus();
                return true;
            }
            if (is_open("building-catalog")) {
                close("building-catalog");
                impl_->hud->GetElementById("build-more")->SetClass("active", false);
                return true;
            }
            if (is_open("stocks-panel")) {
                close("stocks-panel");
                impl_->hud->GetElementById("stocks-toggle")->SetClass("active", false);
                return true;
            }
            if (is_open("profession-panel")) {
                close("profession-panel");
                impl_->hud->GetElementById("profession-open")->Focus();
                return true;
            }
            if (is_open("orders-panel")) {
                close("orders-panel");
                impl_->hud->GetElementById("orders-open")->Focus();
                return true;
            }
            if (is_open("settlement-panel")) {
                close("settlement-panel");
                return true;
            }
            if (is_open("economy-panel")) {
                close("economy-panel");
                return true;
            }
        }
        auto *focused = c->GetFocusElement();
        const bool activation =
            e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE;
        if (activation && focused && focused->GetTagName() == "button") {
            // RmlUi's document performs the native click for a tabbable button.
            // Dispatch once and consume repeats; never insert a chat newline or
            // let the same Space event toggle the simulation beneath the HUD.
            if (!e.key.repeat && focused->IsVisible(true) && !focused->HasAttribute("disabled") &&
                !focused->IsPseudoClassSet("disabled"))
                c->ProcessKeyDown(key_identifier(), m);
            return true;
        }
        if (e.key.repeat && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER))
            return impl_->ime.focused() || !impl_->world;
        p = c->ProcessKeyDown(key_identifier(), m);
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)
            p = c->ProcessTextInput("\n") && p;
        break;
    }
    case SDL_EVENT_KEY_UP:
        p = c->ProcessKeyUp(key_identifier(), m);
        break;
    case SDL_EVENT_TEXT_EDITING:
        impl_->ime.edit(e.edit);
        p = false;
        break;
    case SDL_EVENT_TEXT_INPUT:
        p = c->ProcessTextInput(e.text.text ? e.text.text : "");
        break;
    default:
        break;
    }
    return !p || impl_->ime.focused() || !impl_->world;
}
std::string GameUi::load_status(const SaveLoadInfo& info,std::string_view error) const {
    auto& locale=impl_->localization;
    if(!error.empty())return locale.format("save.failed",{{"version",info.format?info.source_version():locale.text("save.unknown_version")},{"reason",load_error_text(locale,error)}});
    if(!info.copied)return locale.text("status.loaded");
    const auto name=info.save_path.filename().u8string();
    auto status=locale.format("save.migrated",{{"version",info.source_version()},{"file",std::string(reinterpret_cast<const char*>(name.data()),name.size())}});
    if(info.normalized_rations)status+=" "+locale.text("save.rations_reset");
    if(info.neutralized_attacks)status+=" "+locale.text("save.attacks_reset");
    return status;
}
void GameUi::update(ClientSession *s, std::string_view v) {
    if(v.starts_with("save.load_failed|")) {
        const auto end=v.find('|',17);
        if(end!=v.npos){impl_->update(s,impl_->localization.format("save.failed",{{"version",v.substr(17,end-17)=="unknown"?impl_->localization.text("save.unknown_version"):std::string(v.substr(17,end-17))},{"reason",load_error_text(impl_->localization,v.substr(end+1))}}));return;}
    }
    if (v.empty() && s && s->remote() && !s->snapshot_ready()) {
        const std::string connecting = impl_->localization.text(
            s->player_id() ? "network.loading_world" : "network.connecting");
        impl_->update(s, connecting);
        return;
    }
    impl_->update(s, impl_->localization.text_or_literal(v));
}
void GameUi::compose(render::Frame2D &f) {
    struct Guard {
        FrameRender &r;
        ~Guard() { r.end(); }
    } g{impl_->render};
    impl_->render.begin(f);
    impl_->context->Render();
}
std::vector<UiAction> GameUi::take_actions() {
    std::vector<UiAction> r;
    r.swap(impl_->actions);
    return r;
}
void GameUi::show_menu() {
    impl_->show_menu();
}
void GameUi::show_world() {
    impl_->show_world();
}
void GameUi::toggle_hud() {
    if (!impl_->world)
        return;
    impl_->hud_enabled = !impl_->hud_enabled;
    if (impl_->hud_enabled) {
        impl_->hud->Show();
    } else {
        impl_->close_chat();
        impl_->hud->Hide();
    }
}
void GameUi::show_profession() {impl_->open_profession();}
void GameUi::reset_world_state() {
    if (auto *focused = impl_->context->GetFocusElement())
        focused->Blur();
    impl_->reset_profession_form();
    impl_->reset_manual_form();
    impl_->update_settlement(nullptr);
    impl_->update_economy(nullptr,nullptr);
    impl_->update_buildings(nullptr,{});
    impl_->actions.clear();
    impl_->chat.clear();
    impl_->inventory_markup.clear();
    impl_->delivery_markup.clear();
    impl_->delivery_page = 0;
    impl_->delivery_page_count = 1;
    impl_->selection_details_open = false;
    impl_->building_details_open = false;
    impl_->selection_primary_id = 0;
    visible(impl_->hud, "selection-panel", false);
    visible(impl_->hud, "selection-detail-body", false);
    visible(impl_->hud, "building-summary", false);
    visible(impl_->hud, "building-panel", false);
    visible(impl_->hud, "stocks-panel", false);
    visible(impl_->hud, "building-catalog", false);
    impl_->hud->GetElementById("stocks-toggle")->SetClass("active", false);
    text(impl_->hud, "chat-log", "");
    clear_chat_input();
}
bool GameUi::world_visible() const {
    return impl_->world;
}
bool GameUi::hud_visible() const {
    return impl_->world && impl_->hud_enabled;
}
bool GameUi::text_input_focused() const {
    return impl_->ime.focused();
}
const std::string &GameUi::locale() const {
    return impl_->localization.locale();
}
std::string GameUi::format_command_reply(const CommandReply &reply) const {
    auto &localization = impl_->localization;
    // Local worlds populate ambient wildlife as part of startup. That internal bootstrap is not
    // a player command and must not occupy the central feedback strip on a fresh world.
    if (reply.reason == "wilderness.accepted" && reply.residents.empty())
        return {};
    if ((reply.reason.starts_with("alpha.")||reply.reason.starts_with("settlement.")||reply.reason.starts_with("economy.")||reply.reason.starts_with("wilderness.")||reply.reason.starts_with("lobby."))) {
        const auto reason=impl_->production_feedback_text(impl_->bp_session,reply.reason,reply.residents.empty()?0:reply.residents.front().resident_id);
        return reply.status==CommandReplyStatus::Rejected?localization.format("status.order_rejected",{{"reason",reason}}):reason;
    }
    if (!reply.residents.empty() &&
        (reply.status == CommandReplyStatus::Applied || reply.status == CommandReplyStatus::Rejected)) {
        std::size_t applied = 0, deferred = 0, rejected = 0;
        std::array<std::vector<int>, 8> causes;
        std::array<std::vector<int>, 5> control_causes;
        for (const auto &row : reply.control_rejections) {
            const auto cause = static_cast<std::size_t>(row.failure);
            if (cause > 0 && cause < control_causes.size())
                control_causes[cause].push_back(row.resident_id);
        }
        for (const auto &row : reply.residents) {
            if (row.status == ResidentCommandStatus::Applied)
                ++applied;
            else if (row.status == ResidentCommandStatus::Deferred)
                ++deferred;
            else {
                ++rejected;
                const auto cause = static_cast<std::size_t>(row.failure);
                const bool control_conflict = std::any_of(
                    reply.control_rejections.begin(), reply.control_rejections.end(),
                    [&](const auto &rejection) { return rejection.resident_id == row.resident_id; });
                if (!control_conflict && cause < causes.size())
                    causes[cause].push_back(row.resident_id);
            }
        }
        auto message = localization.format(
            "profession.results", {{"applied", applied}, {"deferred", deferred}, {"rejected", rejected}});
        const auto actor_ids = [](const std::vector<int> &ids) {
            std::string suffix = " [";
            for (std::size_t i = 0; i < std::min<std::size_t>(8, ids.size()); ++i)
                suffix += (i ? ", #" : "#") + std::to_string(ids[i]);
            return suffix + (ids.size() > 8 ? ", …]" : "]");
        };
        for (std::size_t i = 1; i < causes.size(); ++i)
            if (!causes[i].empty())
                message += "; " + localization.format("hud.number", {{"count", causes[i].size()}}) + " × " +
                           localization.text(path_failure_key(static_cast<PathFailure>(i))) +
                           actor_ids(causes[i]);
        for (std::size_t i = 1; i < control_causes.size(); ++i)
            if (!control_causes[i].empty()) {
                message += "; " + localization.format("hud.number", {{"count", control_causes[i].size()}}) +
                           " × " + localization.text("manual.control_failure." + std::to_string(i)) +
                           actor_ids(control_causes[i]);
            }
        return message;
    }
    switch (reply.status) {
    case CommandReplyStatus::Queued:
        return localization.text("status.order_queued");
    case CommandReplyStatus::Applied:
        return localization.text("status.order_applied");
    case CommandReplyStatus::Rejected: {
        const auto reason = reply.failure != PathFailure::None
                                ? localization.text(path_failure_key(reply.failure))
                                : (reply.reason.empty() ? localization.text("order.failed") : reply.reason);
        return localization.format("status.order_rejected", {{"reason", reason}});
    }
    case CommandReplyStatus::Invalid:
        return localization.text("status.order_invalid");
    }
    return {};
}
float GameUi::scale() const {
    return impl_->scale;
}
void GameUi::clear_chat_input() {
    dynamic_cast<Rml::ElementFormControlInput *>(impl_->hud->GetElementById("chat-input"))->SetValue("");
}
void GameUi::set_view_state(const ClientSession *session, const std::vector<int> &selected_ids, bool paused,
                            int speed, WorldLayer layer, const std::vector<int>& buildings,
                            bool interaction_active) {
    impl_->set_view_state(session, selected_ids, paused, speed, layer, buildings, interaction_active);
}
void GameUi::presentation_applied() { impl_->presentation_applied(); }
void GameUi::show_help() {
    impl_->show_help();
}
bool GameUi::activate_for_test(std::string_view i) {
    if (auto *e = impl_->element(i)) {
        e->DispatchEvent("click", Rml::Dictionary{});
        return true;
    }
    return false;
}
bool GameUi::focus_for_test(std::string_view i) {
    if (auto *e = impl_->element(i))
        return e->Focus();
    return false;
}
std::string GameUi::focused_element_for_test() const {
    if (auto *e = impl_->context->GetFocusElement())
        return e->GetId();
    return {};
}
std::string GameUi::element_rml_for_test(std::string_view i) const {
    if (auto *e = impl_->element(i))
        return e->GetInnerRML();
    return {};
}
std::string GameUi::select_option_rml_for_test(std::string_view id, int index) const {
    if (auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(impl_->element(id)))
        if (auto *option = select->GetOption(index))
            return option->GetInnerRML();
    return {};
}
bool GameUi::visible_for_test(std::string_view i) const {
    auto *e = impl_->element(i);
    if (!e) return false;
    for (; e; e = e->GetParentNode())
        if (!e->IsVisible()) return false;
    return true;
}
bool GameUi::enabled_for_test(std::string_view i) const {
    const auto *element = impl_->element(i);
    return element && !element->HasAttribute("disabled") &&
           element->GetAttribute<Rml::String>("aria-disabled", "false") != "true";
}
render::Point GameUi::element_center_for_test(std::string_view i) const {
    if (auto *e = impl_->element(i)) {
        auto p = e->GetAbsoluteOffset(Rml::BoxArea::Border), s = e->GetBox().GetSize(Rml::BoxArea::Border);
        return {p.x + s.x / 2, p.y + s.y / 2};
    }
    return {};
}
render::Rect GameUi::element_bounds_for_test(std::string_view i) const {
    if (auto *e = impl_->element(i)) {
        const auto p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = e->GetBox().GetSize(Rml::BoxArea::Border);
        return {p.x, p.y, size.x, size.y};
    }
    return {};
}
float GameUi::element_text_width_for_test(std::string_view i) const {
    auto *element = impl_->element(i);
    if (!element || element->GetNumChildren() != 1)
        return -1.f;
    // Measure the decoded text, not the flex-shrunk span or escaped RML.
    const auto *text = dynamic_cast<Rml::ElementText *>(element->GetChild(0));
    return text ? static_cast<float>(Rml::ElementUtilities::GetStringWidth(element, text->GetText())) : -1.f;
}
} // namespace ribterra

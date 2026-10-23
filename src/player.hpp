#pragma once
#include <windows.h>
#include <cstring>
#include "mpv/client.h"
#include <string>
#include <vector>

inline std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string result(static_cast<size_t>(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr, nullptr);
    return result;
}
inline std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring result(static_cast<size_t>(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
    return result;
}

struct Track {
    int64_t id{};
    std::string type;
    std::wstring label;
    bool selected{};
};
struct Player {
    HMODULE dll{};
    mpv_handle *h{};
    std::wstring failure;
    decltype(&mpv_create) create{};
    decltype(&mpv_initialize) initialize{};
    decltype(&mpv_set_option_string) option{};
    decltype(&mpv_command) command{};
    decltype(&mpv_command_async) commandAsync{};
    decltype(&mpv_get_property) get{};
    decltype(&mpv_set_property) set{};
    decltype(&mpv_set_property_string) setString{};
    decltype(&mpv_get_property_string) getString{};
    decltype(&mpv_free) free_{};
    decltype(&mpv_free_node_contents) freeNode{};
    decltype(&mpv_terminate_destroy) destroy{};
    decltype(&mpv_wait_event) wait{};
    decltype(&mpv_observe_property) observe{};
    decltype(&mpv_error_string) errorString{};
    decltype(&mpv_set_wakeup_callback) setWakeup{};
    template <class T> bool bind(T &field, const char *symbol) {
        field = reinterpret_cast<T>(GetProcAddress(dll, symbol));
        return field != nullptr;
    }
    bool load(const std::wstring &path) {
        dll = LoadLibraryExW(path.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll) {
            DWORD code = GetLastError();
            std::wstring why = code == ERROR_MOD_NOT_FOUND    ? L"file assente o manca una DLL da cui dipende"
                               : code == ERROR_BAD_EXE_FORMAT ? L"non è una DLL a 64 bit"
                                                              : L"errore " + std::to_wstring(code);
            failure = L"Impossibile caricare libmpv-2.dll (" + why + L"). Cercata in: " + path;
            return false;
        }
        if (!(bind(create, "mpv_create") && bind(initialize, "mpv_initialize") &&
              bind(option, "mpv_set_option_string") && bind(command, "mpv_command") &&
              bind(commandAsync, "mpv_command_async") && bind(get, "mpv_get_property") &&
              bind(set, "mpv_set_property") && bind(setString, "mpv_set_property_string") &&
              bind(getString, "mpv_get_property_string") && bind(free_, "mpv_free") &&
              bind(freeNode, "mpv_free_node_contents") && bind(destroy, "mpv_terminate_destroy") &&
              bind(wait, "mpv_wait_event") && bind(observe, "mpv_observe_property") &&
              bind(errorString, "mpv_error_string") && bind(setWakeup, "mpv_set_wakeup_callback"))) {
            failure = L"Build libmpv incompatibile.";
            return false;
        }
        h = create();
        return h != nullptr;
    }
    void close() {
        if (h)
            destroy(h);
        h = nullptr;
        if (dll)
            FreeLibrary(dll);
        dll = nullptr;
    }
    ~Player() {
        close();
    }
    int cmd(const char *a, const char *b = nullptr, const char *c = nullptr, uint64_t reply = 1) {
        if (!h)
            return MPV_ERROR_UNINITIALIZED;
        const char *args[] = {a, b, c, nullptr};
        return commandAsync(h, reply, args);
    }
    double number(const char *key, double fallback = 0) const {
        double result = fallback;
        if (h && get(h, key, MPV_FORMAT_DOUBLE, &result) >= 0)
            return result;
        return fallback;
    }
    bool flag(const char *key, bool fallback = false) const {
        int result = fallback;
        if (h && get(h, key, MPV_FORMAT_FLAG, &result) >= 0)
            return result != 0;
        return fallback;
    }
    std::wstring text(const char *key) const {
        if (!h)
            return {};
        char *value = getString(h, key);
        if (!value)
            return {};
        auto result = wide(value);
        free_(value);
        return result;
    }
    int property(const char *key, const std::string &value) {
        return h ? setString(h, key, value.c_str()) : MPV_ERROR_UNINITIALIZED;
    }
    int seek(double seconds) {
        auto value = std::to_string(seconds);
        return cmd("seek", value.c_str(), "absolute+exact");
    }
    std::vector<Track> tracks() const {
        std::vector<Track> result;
        mpv_node root{};
        if (!h || get(h, "track-list", MPV_FORMAT_NODE, &root) < 0)
            return result;
        if (root.format == MPV_FORMAT_NODE_ARRAY)
            for (int i = 0; i < root.u.list->num; ++i) {
                const auto &item = root.u.list->values[i];
                if (item.format != MPV_FORMAT_NODE_MAP)
                    continue;
                auto field = [&](const char *key) -> const mpv_node * {
                    for (int j = 0; j < item.u.list->num; ++j)
                        if (!strcmp(item.u.list->keys[j], key))
                            return &item.u.list->values[j];
                    return nullptr;
                };
                auto str = [&](const char *key) -> std::string {
                    auto n = field(key);
                    return n && n->format == MPV_FORMAT_STRING ? n->u.string : "";
                };
                auto integer = [&](const char *key) -> int64_t {
                    auto n = field(key);
                    return n && n->format == MPV_FORMAT_INT64 ? n->u.int64 : 0;
                };
                auto boolean = [&](const char *key) {
                    auto n = field(key);
                    return n && n->format == MPV_FORMAT_FLAG && n->u.flag;
                };
                Track t;
                t.id = integer("id");
                t.type = str("type");
                t.selected = boolean("selected");
                t.label = L"#" + std::to_wstring(t.id);
                for (const char *key : {"lang", "title", "codec"}) {
                    auto s = str(key);
                    if (!s.empty())
                        t.label += L" · " + wide(s);
                }
                if (t.type == "video" && integer("demux-w") > 0)
                    t.label += L" · " + std::to_wstring(integer("demux-w")) + L"×" +
                               std::to_wstring(integer("demux-h"));
                if (t.type == "audio" && integer("demux-channel-count") > 0)
                    t.label += L" · " + std::to_wstring(integer("demux-channel-count")) + L" ch";
                if (boolean("external"))
                    t.label += L" · esterna";
                result.push_back(std::move(t));
            }
        freeNode(&root);
        return result;
    }
};

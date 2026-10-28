#pragma once
// Engine component management shared by MpvLight and its setup: finds the latest libmpv build on
// GitHub (shinchiro/mpv-winbuild-cmake), downloads it with WinHTTP, checks the SHA-256 published
// by GitHub, extracts libmpv-2.dll from the .7z and installs it next to MpvLight.exe.
//
// The first part (JSON, release parsing, PE checks) is portable and unit-tested on its own;
// the Windows part is compiled only on Windows.
#include "sevenzip.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace comp {

// ---------------------------------------------------------------------------------------------
// Minimal JSON reader (enough for the GitHub releases API)
// ---------------------------------------------------------------------------------------------
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
    const Json *get(const char *key) const {
        if (type != Object)
            return nullptr;
        for (auto &f : fields)
            if (f.first == key)
                return &f.second;
        return nullptr;
    }
    std::string str(const char *key) const {
        auto v = get(key);
        return v && v->type == String ? v->text : std::string();
    }
    double num(const char *key) const {
        auto v = get(key);
        return v && v->type == Number ? v->number : 0;
    }
};

class JsonParser {
    const std::string &s;
    size_t i = 0;
    int depth = 0;
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    bool lit(const char *w) {
        size_t n = std::char_traits<char>::length(w);
        if (s.compare(i, n, w) != 0)
            return false;
        i += n;
        return true;
    }
    static void utf8(std::string &out, uint32_t cp) {
        if (cp < 0x80)
            out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(uint32_t &v) {
        if (i + 4 > s.size())
            return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= static_cast<uint32_t>(c - 'A' + 10);
            else
                return false;
        }
        return true;
    }
    bool string(std::string &out) {
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size())
                return false;
            char e = s[i++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    uint32_t lo = 0;
                    if (!hex4(lo))
                        return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }
    bool value(Json &v) {
        if (++depth > 64)
            return false;
        ws();
        if (i >= s.size())
            return false;
        char c = s[i];
        bool ok = false;
        if (c == '{') {
            v.type = Json::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
                ok = true;
            } else
                for (;;) {
                    ws();
                    std::string key;
                    if (!string(key))
                        break;
                    ws();
                    if (i >= s.size() || s[i] != ':')
                        break;
                    ++i;
                    Json child;
                    if (!value(child))
                        break;
                    v.fields.emplace_back(std::move(key), std::move(child));
                    ws();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == '}') {
                        ++i;
                        ok = true;
                    }
                    break;
                }
        } else if (c == '[') {
            v.type = Json::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
                ok = true;
            } else
                for (;;) {
                    Json child;
                    if (!value(child))
                        break;
                    v.items.push_back(std::move(child));
                    ws();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == ']') {
                        ++i;
                        ok = true;
                    }
                    break;
                }
        } else if (c == '"') {
            v.type = Json::String;
            ok = string(v.text);
        } else if (lit("true")) {
            v.type = Json::Bool;
            v.boolean = ok = true;
        } else if (lit("false")) {
            v.type = Json::Bool;
            ok = true;
        } else if (lit("null")) {
            ok = true;
        } else {
            size_t start = i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' ||
                                    s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
                ++i;
            if (i > start) {
                v.type = Json::Number;
                v.number = std::strtod(s.substr(start, i - start).c_str(), nullptr);
                ok = true;
            }
        }
        --depth;
        return ok;
    }

  public:
    explicit JsonParser(const std::string &input) : s(input) {}
    bool parse(Json &out) {
        if (!value(out))
            return false;
        ws();
        return i == s.size();
    }
};

// ---------------------------------------------------------------------------------------------
// Releases
// ---------------------------------------------------------------------------------------------
struct Asset {
    std::string name, url, sha256;
    uint64_t size = 0;
};
struct Release {
    std::string tag;
    std::vector<Asset> assets;
};

inline bool parseRelease(const std::string &body, Release &rel, std::string &error) {
    Json root;
    if (!JsonParser(body).parse(root) || root.type != Json::Object) {
        error = "risposta di GitHub non valida";
        return false;
    }
    if (!root.get("assets")) {
        auto msg = root.str("message");
        error = msg.empty() ? "nessuna release trovata" : "GitHub: " + msg;
        return false;
    }
    rel.tag = root.str("tag_name");
    for (auto &a : root.get("assets")->items) {
        Asset asset;
        asset.name = a.str("name");
        asset.url = a.str("browser_download_url");
        asset.size = static_cast<uint64_t>(a.num("size"));
        auto digest = a.str("digest");
        if (digest.rfind("sha256:", 0) == 0)
            asset.sha256 = digest.substr(7);
        if (!asset.name.empty() && asset.url.rfind("https://", 0) == 0)
            rel.assets.push_back(std::move(asset));
    }
    return true;
}

// The dev package carries libmpv-2.dll. "-v3-" builds need an x86-64-v3 CPU (AVX2 & co.).
inline const Asset *pickEngine(const Release &rel, bool preferV3) {
    const Asset *standard = nullptr, *v3 = nullptr;
    for (auto &a : rel.assets) {
        if (a.name.rfind("mpv-dev-x86_64-", 0) != 0 || a.name.size() < 4 ||
            a.name.compare(a.name.size() - 3, 3, ".7z") != 0)
            continue;
        if (a.name.find("-v3-") != std::string::npos) {
            if (!v3)
                v3 = &a;
        } else if (!standard)
            standard = &a;
    }
    if (preferV3 && v3)
        return v3;
    // Never fall back to an AVX2-only DLL on a CPU that cannot execute it.
    return standard ? standard : (preferV3 ? v3 : nullptr);
}

// A real 64-bit Windows DLL: "MZ", a PE header and the x64 machine type.
inline bool isX64Dll(const std::vector<uint8_t> &d) {
    if (d.size() < 0x200 || d[0] != 'M' || d[1] != 'Z')
        return false;
    uint32_t pe = static_cast<uint32_t>(d[0x3C]) | static_cast<uint32_t>(d[0x3D]) << 8 |
                  static_cast<uint32_t>(d[0x3E]) << 16 | static_cast<uint32_t>(d[0x3F]) << 24;
    if (pe > d.size() - 24 || d[pe] != 'P' || d[pe + 1] != 'E' || d[pe + 2] != 0 || d[pe + 3] != 0)
        return false;
    uint16_t machine = static_cast<uint16_t>(d[pe + 4] | d[pe + 5] << 8);
    uint16_t characteristics = static_cast<uint16_t>(d[pe + 22] | d[pe + 23] << 8);
    return machine == 0x8664 && (characteristics & 0x2000) != 0; // IMAGE_FILE_DLL
}

inline std::string toHex(const uint8_t *p, size_t n) {
    static const char *digits = "0123456789abcdef";
    std::string out;
    for (size_t k = 0; k < n; ++k) {
        out += digits[p[k] >> 4];
        out += digits[p[k] & 15];
    }
    return out;
}

} // namespace comp

#ifdef _WIN32
#include <bcrypt.h>
#include <shlobj.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace comp {

inline const wchar_t *kReleaseApi =
    L"https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest";
inline const wchar_t *kEngineDll = L"libmpv-2.dll";
inline const wchar_t *kRegKey = L"Software\\MpvLight";

inline std::wstring widen(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring r(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n);
    return r;
}
inline std::wstring systemError(DWORD code) {
    wchar_t *msg = nullptr;
    HMODULE winhttp = GetModuleHandleW(L"winhttp.dll");
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                  FORMAT_MESSAGE_FROM_SYSTEM |
                  (code >= 12000 && code < 12200 && winhttp ? FORMAT_MESSAGE_FROM_HMODULE : 0);
    FormatMessageW(flags, winhttp, code, 0, reinterpret_cast<wchar_t *>(&msg), 0, nullptr);
    std::wstring s = msg ? msg : L"";
    if (msg)
        LocalFree(msg);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ' || s.back() == L'.'))
        s.pop_back();
    return s.empty() ? L"errore " + std::to_wstring(code) : s + L" (" + std::to_wstring(code) + L")";
}

// Does the CPU run x86-64-v3 code (the "-v3-" builds)? AVX2 is the practical marker.
inline bool cpuSupportsV3() {
#ifndef PF_AVX2_INSTRUCTIONS_AVAILABLE
#define PF_AVX2_INSTRUCTIONS_AVAILABLE 40
#endif
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != FALSE;
}

struct Handle {
    HINTERNET h{};
    Handle() = default;
    explicit Handle(HINTERNET v) : h(v) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() {
        if (h)
            WinHttpCloseHandle(h);
    }
    explicit operator bool() const {
        return h != nullptr;
    }
};

using ByteProgress = std::function<void(uint64_t done, uint64_t total)>;

// HTTPS GET into memory. Follows redirects (GitHub sends downloads to its CDN), honours the
// system proxy, can be cancelled, refuses bodies larger than `limit`.
inline bool httpGet(const std::wstring &url, std::vector<uint8_t> &out, const std::atomic<bool> &cancel,
                    std::wstring &error, const ByteProgress &progress = nullptr, bool api = false,
                    uint64_t limit = 512ull << 20) {
    out.clear();
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t hostName[256] = {}, path[4096] = {};
    parts.lpszHostName = hostName;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4096;
    parts.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        error = L"indirizzo non valido: " + url;
        return false;
    }
    std::wstring object = path;
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Handle session(WinHttpOpen(L"MpvLight/0.5.2 (+https://github.com/Kyo89-sudo/MpvLight)",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) // Windows before 8.1 has no automatic proxy type
        session.h = WinHttpOpen(L"MpvLight/0.5.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = systemError(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session.h, 15000, 15000, 20000, 30000);
    Handle connection(WinHttpConnect(session.h, hostName, parts.nPort, 0));
    Handle request(connection
                       ? WinHttpOpenRequest(connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                       : nullptr);
    if (!request) {
        error = systemError(GetLastError());
        return false;
    }
    std::wstring headers =
        api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
            : L"Accept: application/octet-stream\r\n";
    if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(headers.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        DWORD code = GetLastError();
        error = code == ERROR_WINHTTP_NAME_NOT_RESOLVED || code == ERROR_WINHTTP_CANNOT_CONNECT
                    ? L"connessione a Internet non disponibile (" + std::wstring(hostName) + L")"
                    : systemError(code);
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200 && !(api && status == 404)) {
        error = status == 403 || status == 429
                    ? L"GitHub ha limitato le richieste: riprova tra qualche minuto"
                    : L"il server ha risposto con il codice " + std::to_wstring(status);
        return false;
    }
    uint64_t total = 0;
    wchar_t lengthText[32] = {};
    DWORD lengthSize = sizeof(lengthText);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
                            &lengthSize, WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(lengthText, nullptr, 10);
    if (total > limit) {
        error = L"file troppo grande";
        return false;
    }
    if (total)
        out.reserve(static_cast<size_t>(total));
    std::vector<uint8_t> chunk(256 * 1024);
    for (;;) {
        if (cancel.load()) {
            error = L"operazione annullata";
            return false;
        }
        DWORD got = 0;
        if (!WinHttpReadData(request.h, chunk.data(), static_cast<DWORD>(chunk.size()), &got)) {
            error = systemError(GetLastError());
            return false;
        }
        if (got == 0)
            break;
        out.insert(out.end(), chunk.begin(), chunk.begin() + got);
        if (out.size() > limit) {
            error = L"file troppo grande";
            return false;
        }
        if (progress)
            progress(out.size(), total);
    }
    if (total && out.size() != total) {
        error = L"download incompleto";
        return false;
    }
    if (api && status == 404) {
        error = L"nessuna release trovata su GitHub";
        return false;
    }
    return true;
}

inline std::string sha256(const std::vector<uint8_t> &data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[32] = {};
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    // Feed in 64 MiB slices: BCryptHashData takes a ULONG length.
    for (size_t off = 0; ok && off < data.size(); off += 64u << 20) {
        ULONG n = static_cast<ULONG>(std::min<size_t>(64u << 20, data.size() - off));
        ok = BCryptHashData(hash, const_cast<PUCHAR>(data.data() + off), n, 0) == 0;
    }
    ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok ? toHex(digest, sizeof(digest)) : std::string();
}

inline bool readFile(const std::wstring &path, std::vector<uint8_t> &out, std::wstring &error) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        error = systemError(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > (1ll << 30)) {
        CloseHandle(f);
        error = L"file troppo grande";
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t done = 0;
    bool ok = true;
    while (ok && done < out.size()) {
        DWORD got = 0;
        DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, 16u << 20));
        ok = ReadFile(f, out.data() + done, want, &got, nullptr) && got > 0;
        done += got;
    }
    if (!ok)
        error = systemError(GetLastError());
    CloseHandle(f);
    return ok;
}
inline bool writeFile(const std::wstring &path, const uint8_t *data, size_t size, std::wstring &error) {
    HANDLE f =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        error = systemError(GetLastError());
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (ok && done < size) {
        DWORD put = 0;
        DWORD want = static_cast<DWORD>(std::min<size_t>(size - done, 16u << 20));
        ok = WriteFile(f, data + done, want, &put, nullptr) && put > 0;
        done += put;
    }
    if (ok)
        ok = FlushFileBuffers(f) != FALSE;
    if (!ok)
        error = systemError(GetLastError());
    CloseHandle(f);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

// Writes `target` atomically. If the current file is in use (MpvLight has libmpv loaded), it is
// renamed to "<name>.old" first, which Windows allows for mapped DLLs; `replacedInUse` tells the
// caller that a restart is needed. Stale ".old" files are removed by removeStale().
inline std::wstring staleName(const std::wstring &target, int i) {
    return target + L".old" + (i ? std::to_wstring(i) : std::wstring());
}
inline bool replaceFile(const std::wstring &target, const std::vector<uint8_t> &data, bool &replacedInUse,
                        std::wstring &error) {
    replacedInUse = false;
    std::wstring fresh = target + L".new";
    if (!writeFile(fresh, data.data(), data.size(), error))
        return false;
    if (MoveFileExW(fresh.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DWORD firstError = GetLastError();
    // The target is probably loaded (a DLL in use) or running: move it aside under a free ".old" name
    // (an older ".old" may itself still be loaded by another instance), then put the new file in place.
    for (int i = 0; i < 10; ++i) {
        std::wstring old = staleName(target, i);
        DeleteFileW(old.c_str());
        if (!MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
            continue;
        if (MoveFileExW(fresh.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
            replacedInUse = true;
            return true;
        }
        firstError = GetLastError();
        MoveFileExW(old.c_str(), target.c_str(), 0); // put the original back: never leave no file at all
        break;
    }
    error = systemError(firstError);
    DeleteFileW(fresh.c_str());
    return false;
}
// Removes leftovers of previous replacements (those still loaded somewhere simply stay).
inline void removeStale(const std::wstring &dir) {
    for (const wchar_t *name : {kEngineDll, L"MpvLight.exe"}) {
        std::wstring target = dir + L"\\" + name;
        DeleteFileW((target + L".new").c_str());
        for (int i = 0; i < 10; ++i)
            DeleteFileW(staleName(target, i).c_str());
    }
}

inline std::wstring regRead(const wchar_t *name) {
    wchar_t buf[512] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    return buf;
}
inline void regWrite(const wchar_t *name, const std::wstring &value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, name, REG_SZ, value.c_str(),
                    static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}
// The installed engine build, e.g. "mpv-dev-x86_64-20261021-git-e471496c56.7z".
inline std::wstring installedBuild() {
    return regRead(L"EngineBuild");
}

// ---------------------------------------------------------------------------------------------
// Engine installation job (runs on a worker thread; reports through `report`)
// ---------------------------------------------------------------------------------------------
enum class Stage { Query, Download, Verify, Extract, Install, Done };
struct Report {
    Stage stage = Stage::Query;
    double fraction = -1; // -1 = indeterminate
    std::wstring text;
};
struct EngineResult {
    bool ok = false, restartNeeded = false;
    std::wstring error, build;
};
using Reporter = std::function<void(const Report &)>;

inline std::wstring megabytes(uint64_t bytes) {
    wchar_t b[32];
    swprintf_s(b, L"%.1f", static_cast<double>(bytes) / (1024.0 * 1024.0));
    std::wstring s = b;
    for (auto &c : s)
        if (c == L'.')
            c = L',';
    return s + L" MB";
}

inline bool fetchLatest(Release &rel, const std::atomic<bool> &cancel, std::wstring &error) {
    std::vector<uint8_t> body;
    if (!httpGet(kReleaseApi, body, cancel, error, nullptr, true, 8u << 20))
        return false;
    std::string json(body.begin(), body.end()), err;
    if (!parseRelease(json, rel, err)) {
        error = widen(err);
        return false;
    }
    return true;
}

// Installs libmpv-2.dll into `dir`. Source: `localFile` (.7z or .dll) when given, otherwise
// `asset` (or the latest release when `asset` is empty).
inline EngineResult installEngine(const std::wstring &dir, const std::wstring &localFile, Asset asset,
                                  bool preferV3, const std::atomic<bool> &cancel, const Reporter &report) {
    EngineResult res;
    auto notify = [&](Stage s, double f, const std::wstring &t) {
        if (report)
            report(Report{s, f, t});
    };
    std::vector<uint8_t> payload;
    std::wstring sourceName;
    if (!localFile.empty()) {
        notify(Stage::Download, -1, L"Lettura del file…");
        if (!readFile(localFile, payload, res.error))
            return res;
        auto slash = localFile.find_last_of(L"\\/");
        sourceName = slash == std::wstring::npos ? localFile : localFile.substr(slash + 1);
    } else {
        if (asset.url.empty()) {
            notify(Stage::Query, -1, L"Ricerca dell'ultima versione su GitHub…");
            Release rel;
            if (!fetchLatest(rel, cancel, res.error))
                return res;
            auto pick = pickEngine(rel, preferV3);
            if (!pick) {
                res.error = L"la release " + widen(rel.tag) + L" non contiene la libreria per Windows x64";
                return res;
            }
            asset = *pick;
        }
        sourceName = widen(asset.name);
        notify(Stage::Download, 0, L"Scaricamento di " + sourceName + L"…");
        bool ok = httpGet(widen(asset.url), payload, cancel, res.error, [&](uint64_t done, uint64_t total) {
            uint64_t all = total ? total : asset.size;
            notify(Stage::Download, all ? static_cast<double>(done) / static_cast<double>(all) : -1,
                   L"Scaricamento del motore: " + megabytes(done) + (all ? L" di " + megabytes(all) : L""));
        });
        if (!ok)
            return res;
        if (!asset.sha256.empty()) {
            notify(Stage::Verify, -1, L"Verifica dell'integrità (SHA-256)…");
            if (sha256(payload) != asset.sha256) {
                res.error = L"il file scaricato non corrisponde all'impronta pubblicata da GitHub";
                return res;
            }
        }
    }
    if (cancel.load()) {
        res.error = L"operazione annullata";
        return res;
    }
    std::vector<uint8_t> dll;
    bool isDll = payload.size() > 2 && payload[0] == 'M' && payload[1] == 'Z';
    if (isDll)
        dll = std::move(payload);
    else {
        notify(Stage::Extract, -1, L"Estrazione di libmpv-2.dll…");
        std::u16string inside;
        std::string err;
        if (!sevenzip::extractFile(payload, u"libmpv-2.dll", dll, inside, err)) {
            res.error = L"estrazione non riuscita: " + widen(err);
            return res;
        }
        payload.clear();
        payload.shrink_to_fit();
    }
    if (!isX64Dll(dll)) {
        res.error = L"il file non è una libreria Windows a 64 bit";
        return res;
    }
    notify(Stage::Install, -1, L"Installazione del motore…");
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (!replaceFile(dir + L"\\" + kEngineDll, dll, res.restartNeeded, res.error))
        return res;
    res.build = sourceName;
    regWrite(L"EngineBuild", sourceName);
    res.ok = true;
    notify(Stage::Done, 1, L"Motore installato");
    return res;
}

} // namespace comp
#endif

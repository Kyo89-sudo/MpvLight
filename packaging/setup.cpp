// MpvLight Setup 0.5.2 — installer and uninstaller for the current user (no administrator rights).
//
// Installs MpvLight.exe (embedded as a resource), downloads the libmpv engine from GitHub through
// components.hpp, creates shortcuts, "Open with" associations and the entry in Settings > Apps.
// "MpvLightSetup.exe /uninstall" (registered as UninstallString) removes everything again.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <cstring>
#include <dwmapi.h>
#include <memory>
#include <mutex>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <thread>
#include <uxtheme.h>
#include <vector>
#include <windowsx.h>
namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus
#include <gdiplus.h>
// Last: the LZMA SDK headers it pulls in define the macros True/False.
#include "../src/components.hpp"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

static const wchar_t *kVersion = L"0.5.2";
static const wchar_t *kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MpvLight";
static const wchar_t *kInnoKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MpvLight_is1";
static const wchar_t *kProgId = L"MpvLight.Video";
static const wchar_t *kExtensions[] = {L".mkv",  L".mp4",  L".avi", L".webm", L".mov",  L".m4v", L".ts",
                                       L".m2ts", L".wmv",  L".flv", L".mpg",  L".mpeg", L".3gp", L".ogv",
                                       L".mp3",  L".flac", L".ogg", L".opus", L".m4a",  L".wav", L".mka"};
struct Payload {
    int id;
    const wchar_t *name;
};
static const Payload kPayload[] = {
    {101, L"MpvLight.exe"}, {102, L"README.md"}, {103, L"CHANGELOG.md"}, {104, L"LICENSE-GPL.txt"}};

constexpr COLORREF cBack = RGB(22, 21, 29), cField = RGB(31, 29, 40), cHover = RGB(44, 41, 57);
constexpr COLORREF cText = RGB(240, 238, 248), cMuted = RGB(160, 156, 180), cFaint = RGB(112, 108, 132);
constexpr COLORREF cViolet = RGB(124, 58, 237), cPink = RGB(219, 39, 119), cLilac = RGB(192, 132, 252);
constexpr COLORREF cError = RGB(248, 113, 113), cGood = RGB(134, 239, 172);

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
enum class Page { Options, Working, Done, Failed, UninstallAsk, UninstallDone };
struct Options {
    std::wstring dir, localEngine;
    bool startMenu = true, desktop = false, associate = true, engine = true, launch = true,
         removeSettings = false;
};
struct Job {
    std::mutex lock;
    std::atomic<bool> cancel{false};
    bool finished = false, ok = false;
    std::wstring text, error, engineError;
    double fraction = 0;
};

HINSTANCE inst{};
HWND win{}, edit{};
HFONT editFont{};
HBRUSH fieldBrush{};
HICON headerIcon{};
int dpi = 96;
Page page = Page::Options;
Options opt;
bool uninstallMode = false, upgrade = false, enginePresent = false, silent = false;
std::wstring tempSelf; // set when the uninstaller runs from a temporary copy
std::shared_ptr<Job> job;
std::wstring jobText, jobError, engineError;
double jobFraction = 0;
int hover = -1;
enum Hit {
    HPrimary,
    HSecondary,
    HBrowse,
    HStart,
    HDesktop,
    HAssoc,
    HEngine,
    HLocal,
    HLaunch,
    HSettings,
    HRetry,
    HCount
};
RectF hits[HCount];
ULONG_PTR gdiplusToken{};
struct Fonts {
    std::unique_ptr<Font> hero, title, text, caption;
} fonts;

int px(int v) {
    return MulDiv(v, dpi, 96);
}
float fx(float v) {
    return v * static_cast<float>(dpi) / 96.0f;
}
Color argb(int a, COLORREF c) {
    return Color(static_cast<BYTE>(std::clamp(a, 0, 255)), GetRValue(c), GetGValue(c), GetBValue(c));
}

// ---------------------------------------------------------------------------------------------
// File-system and registry helpers
// ---------------------------------------------------------------------------------------------
std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)))
        r = p;
    CoTaskMemFree(p);
    return r;
}
bool exists(const std::wstring &p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
std::wstring selfPath() {
    wchar_t p[32768];
    DWORD n = GetModuleFileNameW(nullptr, p, 32768);
    return std::wstring(p, n);
}
std::wstring defaultDir() {
    auto saved = comp::regRead(L"InstallDir");
    if (!saved.empty())
        return saved;
    return knownFolder(FOLDERID_LocalAppData) + L"\\Programs\\MpvLight";
}
void setString(HKEY root, const std::wstring &key, const wchar_t *name, const std::wstring &value) {
    RegSetKeyValueW(root, key.c_str(), name, REG_SZ, value.c_str(),
                    static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}
void setDword(HKEY root, const std::wstring &key, const wchar_t *name, DWORD value) {
    RegSetKeyValueW(root, key.c_str(), name, REG_DWORD, &value, sizeof(value));
}
void deleteTree(const std::wstring &key) {
    RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
}
void deleteValue(const std::wstring &key, const wchar_t *name) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, name);
        RegCloseKey(k);
    }
}
bool removeTree(const std::wstring &dir) {
    if (!exists(dir))
        return true;
    std::wstring from = dir;
    from.push_back(L'\0'); // double-NUL terminated list
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    return SHFileOperationW(&op) == 0;
}
bool makeShortcut(const std::wstring &link, const std::wstring &target, const std::wstring &args,
                  const std::wstring &description) {
    IShellLinkW *sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl))))
        return false;
    sl->SetPath(target.c_str());
    sl->SetArguments(args.c_str());
    sl->SetWorkingDirectory(target.substr(0, target.find_last_of(L'\\')).c_str());
    sl->SetDescription(description.c_str());
    sl->SetIconLocation(target.c_str(), 0);
    IPersistFile *pf = nullptr;
    bool ok = SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) && SUCCEEDED(pf->Save(link.c_str(), TRUE));
    if (pf)
        pf->Release();
    sl->Release();
    return ok;
}
// Asks every running MpvLight to close and waits a little.
bool closeRunning() {
    for (int round = 0; round < 80; ++round) {
        HWND w = FindWindowW(L"MpvLightWindow", nullptr);
        if (!w)
            return true;
        if (round % 20 == 0)
            PostMessageW(w, WM_CLOSE, 0, 0);
        Sleep(100);
    }
    return FindWindowW(L"MpvLightWindow", nullptr) == nullptr;
}
bool resourceBytes(int id, std::vector<uint8_t> &out) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL h = r ? LoadResource(nullptr, r) : nullptr;
    auto data = h ? static_cast<const uint8_t *>(LockResource(h)) : nullptr;
    if (!data)
        return false;
    out.assign(data, data + SizeofResource(nullptr, r));
    return true;
}
uint64_t folderSize(const std::wstring &dir) {
    uint64_t total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            total += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return total;
}

// ---------------------------------------------------------------------------------------------
// Install / uninstall steps (worker thread)
// ---------------------------------------------------------------------------------------------
void registerAssociations(const std::wstring &exe) {
    std::wstring classes = L"Software\\Classes\\";
    std::wstring open = L"\"" + exe + L"\" \"%1\"";
    setString(HKEY_CURRENT_USER, classes + kProgId, nullptr, L"File multimediale (MpvLight)");
    setString(HKEY_CURRENT_USER, classes + kProgId + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    setString(HKEY_CURRENT_USER, classes + kProgId + L"\\shell\\open\\command", nullptr, open);
    std::wstring app = classes + L"Applications\\MpvLight.exe";
    setString(HKEY_CURRENT_USER, app, L"FriendlyAppName", L"MpvLight");
    setString(HKEY_CURRENT_USER, app + L"\\shell\\open\\command", nullptr, open);
    std::wstring caps = L"Software\\Clients\\Media\\MpvLight\\Capabilities";
    setString(HKEY_CURRENT_USER, caps, L"ApplicationName", L"MpvLight");
    setString(HKEY_CURRENT_USER, caps, L"ApplicationDescription",
              L"Lettore video e audio leggero basato su mpv");
    for (auto ext : kExtensions) {
        setString(HKEY_CURRENT_USER, classes + ext + L"\\OpenWithProgids", kProgId, L"");
        setString(HKEY_CURRENT_USER, app + L"\\SupportedTypes", ext, L"");
        setString(HKEY_CURRENT_USER, caps + L"\\FileAssociations", ext, kProgId);
    }
    setString(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"MpvLight", caps);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
void unregisterAssociations() {
    std::wstring classes = L"Software\\Classes\\";
    for (auto ext : kExtensions)
        deleteValue(classes + ext + L"\\OpenWithProgids", kProgId);
    deleteTree(classes + kProgId);
    deleteTree(classes + L"Applications\\MpvLight.exe");
    deleteTree(L"Software\\Clients\\Media\\MpvLight");
    deleteValue(L"Software\\RegisteredApplications", L"MpvLight");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
// Leftovers of the 0.3 Inno Setup installer. Files that Inno put there ("source" folder, TESTING.md)
// are removed only when this folder really holds an Inno installation of MpvLight.
void removeInnoLeftovers(const std::wstring &dir) {
    bool inno = exists(dir + L"\\unins000.dat") || exists(dir + L"\\unins000.exe");
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInnoKey, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t location[1024] = {};
        DWORD size = sizeof(location);
        if (RegGetValueW(k, nullptr, L"InstallLocation", RRF_RT_REG_SZ, nullptr, location, &size) ==
            ERROR_SUCCESS) {
            std::wstring loc = location;
            while (!loc.empty() && loc.back() == L'\\')
                loc.pop_back();
            inno = inno || lstrcmpiW(loc.c_str(), dir.c_str()) == 0;
        }
        RegCloseKey(k);
        deleteTree(kInnoKey);
    }
    if (inno) {
        for (auto f : {L"unins000.exe", L"unins000.dat", L"TESTING.md"})
            DeleteFileW((dir + L"\\" + f).c_str());
        removeTree(dir + L"\\source");
    }
    std::wstring group = knownFolder(FOLDERID_Programs) + L"\\MpvLight";
    DeleteFileW((group + L"\\MpvLight.lnk").c_str());
    RemoveDirectoryW(group.c_str());
}
void say(const std::shared_ptr<Job> &j, const std::wstring &text, double fraction) {
    std::lock_guard<std::mutex> hold(j->lock);
    j->text = text;
    j->fraction = fraction;
}
void installWorker(Options o, std::shared_ptr<Job> j, bool engineOnly) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring error;
    auto fail = [&](const std::wstring &e) {
        std::lock_guard<std::mutex> hold(j->lock);
        j->error = e;
        j->finished = true;
    };
    std::wstring exe = o.dir + L"\\MpvLight.exe";
    if (!engineOnly) {
        say(j, L"Chiusura di MpvLight…", 0.01);
        if (!closeRunning()) {
            fail(L"MpvLight è ancora aperto. Chiudilo e riprova.");
            CoUninitialize();
            return;
        }
        say(j, L"Copia dei file…", 0.03);
        if (SHCreateDirectoryExW(nullptr, o.dir.c_str(), nullptr) != ERROR_SUCCESS && !exists(o.dir)) {
            fail(L"Impossibile creare la cartella " + o.dir);
            CoUninitialize();
            return;
        }
        for (auto &p : kPayload) {
            std::vector<uint8_t> bytes;
            bool inUse = false;
            if (!resourceBytes(p.id, bytes)) {
                fail(std::wstring(L"Il setup è incompleto: manca ") + p.name);
                CoUninitialize();
                return;
            }
            if (!comp::replaceFile(o.dir + L"\\" + p.name, bytes, inUse, error)) {
                fail(std::wstring(L"Impossibile scrivere ") + p.name + L": " + error);
                CoUninitialize();
                return;
            }
        }
        // Keep a copy of the setup as uninstaller (unless we are already running it from there).
        std::wstring keep = o.dir + L"\\MpvLightSetup.exe";
        if (lstrcmpiW(selfPath().c_str(), keep.c_str()) != 0) {
            std::vector<uint8_t> me;
            bool inUse = false;
            if (comp::readFile(selfPath(), me, error))
                comp::replaceFile(keep, me, inUse, error);
        }
        say(j, L"File copiati", 0.10);
    }
    if (j->cancel.load()) {
        fail(L"Installazione annullata.");
        CoUninitialize();
        return;
    }
    if (o.engine || !exists(o.dir + L"\\" + comp::kEngineDll)) {
        auto result = comp::installEngine(o.dir, o.localEngine, comp::Asset{}, comp::cpuSupportsV3(),
                                          j->cancel, [&](const comp::Report &r) {
                                              double f = r.fraction < 0 ? -1 : 0.10 + 0.80 * r.fraction;
                                              say(j, r.text, f);
                                          });
        if (!result.ok) {
            if (j->cancel.load()) {
                fail(L"Installazione annullata.");
                CoUninitialize();
                return;
            }
            std::lock_guard<std::mutex> hold(j->lock);
            j->engineError = result.error;
        }
    }
    if (!engineOnly) {
        say(j, L"Collegamenti e associazioni…", 0.93);
        std::wstring startLink = knownFolder(FOLDERID_Programs) + L"\\MpvLight.lnk";
        std::wstring deskLink = knownFolder(FOLDERID_Desktop) + L"\\MpvLight.lnk";
        if (o.startMenu)
            makeShortcut(startLink, exe, L"", L"Lettore video e audio");
        else
            DeleteFileW(startLink.c_str());
        if (o.desktop)
            makeShortcut(deskLink, exe, L"", L"Lettore video e audio");
        else
            DeleteFileW(deskLink.c_str());
        if (o.associate)
            registerAssociations(exe);
        else
            unregisterAssociations();
        removeInnoLeftovers(o.dir);
        std::wstring setup = o.dir + L"\\MpvLightSetup.exe";
        setString(HKEY_CURRENT_USER, kUninstallKey, L"DisplayName", L"MpvLight");
        setString(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion", kVersion);
        setString(HKEY_CURRENT_USER, kUninstallKey, L"Publisher", L"MpvLight");
        setString(HKEY_CURRENT_USER, kUninstallKey, L"DisplayIcon", exe + L",0");
        setString(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", o.dir);
        setString(HKEY_CURRENT_USER, kUninstallKey, L"UninstallString", L"\"" + setup + L"\" /uninstall");
        setString(HKEY_CURRENT_USER, kUninstallKey, L"QuietUninstallString",
                  L"\"" + setup + L"\" /uninstall /S");
        setDword(HKEY_CURRENT_USER, kUninstallKey, L"NoModify", 1);
        setDword(HKEY_CURRENT_USER, kUninstallKey, L"NoRepair", 1);
        setDword(HKEY_CURRENT_USER, kUninstallKey, L"EstimatedSize",
                 static_cast<DWORD>(folderSize(o.dir) / 1024));
        comp::regWrite(L"InstallDir", o.dir);
        comp::regWrite(L"Version", kVersion);
    }
    say(j, L"Fatto", 1.0);
    {
        std::lock_guard<std::mutex> hold(j->lock);
        j->ok = true;
        j->finished = true;
    }
    CoUninitialize();
}
void uninstallWorker(Options o, std::shared_ptr<Job> j) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    say(j, L"Chiusura di MpvLight…", 0.1);
    if (!closeRunning()) {
        std::lock_guard<std::mutex> hold(j->lock);
        j->error = L"MpvLight è ancora aperto. Chiudilo e riprova.";
        j->finished = true;
        CoUninitialize();
        return;
    }
    say(j, L"Rimozione dei file…", 0.4);
    for (auto f : {L"MpvLight.exe", L"libmpv-2.dll", L"README.md", L"CHANGELOG.md", L"LICENSE-GPL.txt",
                   L"MpvLightSetup.exe"})
        DeleteFileW((o.dir + L"\\" + f).c_str());
    comp::removeStale(o.dir);
    removeInnoLeftovers(o.dir);
    RemoveDirectoryW(o.dir.c_str()); // only if empty: never delete files we did not install
    say(j, L"Rimozione di collegamenti e associazioni…", 0.7);
    DeleteFileW((knownFolder(FOLDERID_Programs) + L"\\MpvLight.lnk").c_str());
    DeleteFileW((knownFolder(FOLDERID_Desktop) + L"\\MpvLight.lnk").c_str());
    removeInnoLeftovers(o.dir);
    unregisterAssociations();
    deleteTree(kUninstallKey);
    deleteTree(comp::kRegKey);
    if (o.removeSettings)
        removeTree(knownFolder(FOLDERID_LocalAppData) + L"\\MpvLight");
    say(j, L"Fatto", 1.0);
    std::lock_guard<std::mutex> hold(j->lock);
    j->ok = true;
    j->finished = true;
    CoUninitialize();
}

// ---------------------------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------------------------
void makeFonts() {
    auto make = [](const wchar_t *family, float size) {
        return std::make_unique<Font>(family, fx(size), FontStyleRegular, UnitPixel);
    };
    FontFamily semi(L"Segoe UI Semibold");
    const wchar_t *semiName = semi.IsAvailable() ? L"Segoe UI Semibold" : L"Segoe UI";
    fonts.hero = make(semiName, 26);
    fonts.title = make(semiName, 15);
    fonts.text = make(L"Segoe UI", 14);
    fonts.caption = make(L"Segoe UI", 12);
    if (editFont)
        DeleteObject(editFont);
    editFont =
        CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (edit)
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    if (headerIcon)
        DestroyIcon(headerIcon);
    headerIcon = nullptr;
    LoadIconWithScaleDown(inst, MAKEINTRESOURCEW(1), px(56), px(56), &headerIcon);
}
float measure(const std::wstring &s, const Font *f) {
    Bitmap scratch(1, 1, PixelFormat32bppPARGB);
    Graphics g(&scratch);
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
    RectF out;
    g.MeasureString(s.c_str(), static_cast<INT>(s.size()), f, PointF(0, 0), &fmt, &out);
    return out.Width;
}
void text(Graphics &g, const std::wstring &s, const Font *f, const RectF &r, Color c, bool wrap = false,
          StringAlignment h = StringAlignmentNear) {
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(wrap ? StringAlignmentNear : StringAlignmentCenter);
    if (!wrap) {
        fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
        fmt.SetTrimming(StringTrimmingEllipsisPath);
    }
    SolidBrush b(c);
    g.DrawString(s.c_str(), static_cast<INT>(s.size()), f, r, &fmt, &b);
}
void roundRect(Graphics &g, const RectF &r, float radius, const Brush &b) {
    GraphicsPath path;
    float d = std::min(radius * 2, std::min(r.Width, r.Height));
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    g.FillPath(&b, &path);
}
void roundRect(Graphics &g, const RectF &r, float radius, Color c) {
    SolidBrush b(c);
    roundRect(g, r, radius, b);
}
void button(Graphics &g, Hit id, const std::wstring &label, float right, float y, bool primary) {
    float w = measure(label, fonts.title.get()) + fx(primary ? 52.0f : 40.0f), h = fx(38);
    hits[id] = RectF(right - w, y, w, h);
    if (primary) {
        bool hot = hover == id;
        LinearGradientBrush b(PointF(right - w - 1, 0), PointF(right + 1, 0),
                              argb(255, hot ? RGB(147, 87, 255) : cViolet),
                              argb(255, hot ? RGB(240, 70, 150) : cPink));
        roundRect(g, hits[id], h / 2, b);
    } else
        roundRect(g, hits[id], h / 2, argb(255, hover == id ? cHover : cField));
    text(g, label, fonts.title.get(), hits[id], argb(255, primary ? RGB(255, 255, 255) : cText), false,
         StringAlignmentCenter);
}
void toggle(Graphics &g, Hit id, const std::wstring &label, const std::wstring &note, bool on, float x,
            float y, float w, bool enabled = true) {
    hits[id] = enabled ? RectF(x - fx(6), y - fx(4), w + fx(12), fx(34)) : RectF();
    if (hover == id)
        roundRect(g, hits[id], fx(8), argb(255, RGB(27, 26, 35)));
    RectF sw(x, y + fx(5), fx(38), fx(22));
    if (on) {
        LinearGradientBrush b(PointF(sw.X - 1, 0), PointF(sw.GetRight() + 1, 0),
                              argb(enabled ? 255 : 120, cViolet), argb(enabled ? 255 : 120, cPink));
        roundRect(g, sw, sw.Height / 2, b);
    } else
        roundRect(g, sw, sw.Height / 2, argb(255, RGB(58, 55, 72)));
    float k = fx(16), kx = on ? sw.GetRight() - k - fx(3) : sw.X + fx(3);
    SolidBrush knob(argb(enabled ? 255 : 150, RGB(255, 255, 255)));
    g.FillEllipse(&knob, kx, sw.Y + fx(3), k, k);
    float tx = x + fx(52);
    text(g, label, fonts.text.get(), RectF(tx, y, w - fx(52), fx(32)), argb(255, enabled ? cText : cMuted));
    if (!note.empty()) {
        float lw = measure(label, fonts.text.get());
        text(g, note, fonts.caption.get(), RectF(tx + lw + fx(8), y + fx(1), w - fx(60) - lw, fx(32)),
             argb(255, cFaint));
    }
}
void progressBar(Graphics &g, float x, float y, float w, double fraction) {
    RectF track(x, y, w, fx(8));
    roundRect(g, track, fx(4), argb(255, cField));
    float frac = fraction < 0 ? 0.3f : static_cast<float>(std::clamp(fraction, 0.0, 1.0));
    float start = fraction < 0 ? static_cast<float>((GetTickCount64() % 1400) / 1400.0) * (1 - frac) : 0;
    if (frac <= 0)
        return;
    LinearGradientBrush b(PointF(x - 1, 0), PointF(x + w + 1, 0), argb(255, cViolet), argb(255, cPink));
    roundRect(g, RectF(x + w * start, y, std::max(fx(8), w * frac), fx(8)), fx(4), b);
}
void placeEdit() {
    if (!edit)
        return;
    bool visible = page == Page::Options;
    RECT rc;
    GetClientRect(win, &rc);
    int x = px(32), y = px(135), w = rc.right - px(32) - px(28) - px(118) - px(8);
    MoveWindow(edit, x + px(10), y + px(8), w - px(20), px(20), TRUE);
    ShowWindow(edit, visible ? SW_SHOW : SW_HIDE);
}
void paint() {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(win, &ps);
    RECT rc;
    GetClientRect(win, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ old = SelectObject(dc, bmp);
    float W = static_cast<float>(rc.right), H = static_cast<float>(rc.bottom);
    for (auto &h : hits)
        h = RectF();
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        SolidBrush bg(argb(255, cBack));
        g.FillRectangle(&bg, 0.0f, 0.0f, W, H);
        // Header
        if (headerIcon) {
            HDC idc = g.GetHDC();
            DrawIconEx(idc, px(28), px(24), headerIcon, px(56), px(56), 0, nullptr, DI_NORMAL);
            g.ReleaseHDC(idc);
        }
        text(g, L"MpvLight", fonts.hero.get(), RectF(fx(98), fx(24), W - fx(120), fx(34)), argb(255, cText));
        std::wstring sub = std::wstring(L"Versione ") + kVersion + L"  ·  " +
                           (uninstallMode ? L"Disinstallazione"
                            : upgrade     ? L"Aggiornamento"
                                          : L"Installazione");
        text(g, sub, fonts.text.get(), RectF(fx(99), fx(58), W - fx(120), fx(22)), argb(255, cMuted));
        SolidBrush rule(argb(255, RGB(38, 36, 48)));
        g.FillRectangle(&rule, fx(28), fx(98), W - fx(56), fx(1));
        float x = fx(32), cw = W - fx(64), by = H - fx(62), right = W - fx(28);
        switch (page) {
        case Page::Options: {
            text(g, L"Cartella di installazione", fonts.caption.get(), RectF(x, fx(110), cw, fx(20)),
                 argb(255, cFaint));
            RectF field(x, fx(135), cw - fx(126), fx(36));
            roundRect(g, field, fx(8), argb(255, cField));
            button(g, HBrowse, L"Sfoglia…", W - fx(28), fx(134), false);
            float y = fx(190);
            toggle(g, HStart, L"Collegamento nel menu Start", L"", opt.startMenu, x, y, cw);
            y += fx(38);
            toggle(g, HDesktop, L"Collegamento sul desktop", L"", opt.desktop, x, y, cw);
            y += fx(38);
            toggle(g, HAssoc, L"Aggiungi MpvLight ad \"Apri con\"", L"video e audio", opt.associate, x, y,
                   cw);
            y += fx(38);
            std::wstring engineNote = !opt.localEngine.empty() ? L"dal file scelto"
                                      : enginePresent          ? L"ultima versione da GitHub"
                                                               : L"necessario · circa 30 MB da GitHub";
            toggle(g, HEngine, enginePresent ? L"Aggiorna il motore libmpv" : L"Scarica il motore libmpv",
                   engineNote, opt.engine || !enginePresent, x, y, cw, enginePresent);
            y += fx(42);
            std::wstring local = opt.localEngine.empty()
                                     ? L"Usa un file già scaricato (.7z o .dll)…"
                                     : L"File: " +
                                           opt.localEngine.substr(opt.localEngine.find_last_of(L'\\') + 1) +
                                           L"  (cambia)";
            hits[HLocal] = RectF(x + fx(52), y, measure(local, fonts.caption.get()) + fx(4), fx(22));
            text(g, local, fonts.caption.get(), hits[HLocal],
                 argb(255, hover == HLocal ? RGB(216, 180, 254) : cLilac));
            button(g, HPrimary, upgrade ? L"Aggiorna" : L"Installa", right, by, true);
            button(g, HSecondary, L"Annulla", hits[HPrimary].X - fx(10), by, false);
            break;
        }
        case Page::Working:
            text(g, uninstallMode ? L"Disinstallazione in corso" : L"Installazione in corso",
                 fonts.title.get(), RectF(x, fx(124), cw, fx(26)), argb(255, cText));
            text(g, jobText, fonts.text.get(), RectF(x, fx(160), cw, fx(24)), argb(255, cMuted));
            progressBar(g, x, fx(196), cw, jobFraction);
            if (!uninstallMode)
                text(g,
                     L"Il motore viene scaricato dalle build pubbliche di mpv per Windows "
                     L"(github.com/shinchiro/mpv-winbuild-cmake) e verificato con l'impronta SHA-256.",
                     fonts.caption.get(), RectF(x, fx(226), cw, fx(44)), argb(255, cFaint), true);
            if (!uninstallMode)
                button(g, HSecondary, L"Annulla", right, by, false);
            break;
        case Page::Done: {
            text(g, upgrade ? L"Aggiornamento completato" : L"Installazione completata", fonts.title.get(),
                 RectF(x, fx(124), cw, fx(26)), argb(255, engineError.empty() ? cGood : cText));
            std::wstring msg = L"MpvLight è in " + opt.dir + L".";
            text(g, msg, fonts.text.get(), RectF(x, fx(158), cw, fx(44)), argb(255, cMuted), true);
            float y = fx(206);
            if (!engineError.empty()) {
                text(g, L"Il motore libmpv non è stato installato: " + engineError, fonts.text.get(),
                     RectF(x, y, cw, fx(60)), argb(255, cError), true);
                y += fx(62);
                button(g, HRetry, L"Riprova a scaricarlo",
                       x + measure(L"Riprova a scaricarlo", fonts.title.get()) + fx(40), y, false);
                std::wstring local = L"Usa un file già scaricato…";
                hits[HLocal] = RectF(hits[HRetry].GetRight() + fx(16), y + fx(8),
                                     measure(local, fonts.text.get()) + fx(4), fx(22));
                text(g, local, fonts.text.get(), hits[HLocal],
                     argb(255, hover == HLocal ? RGB(216, 180, 254) : cLilac));
                y += fx(50);
            }
            toggle(g, HLaunch, L"Avvia MpvLight", L"", opt.launch, x, std::max(y, H - fx(120)), cw);
            button(g, HPrimary, L"Fine", right, by, true);
            break;
        }
        case Page::Failed:
            text(g, L"Qualcosa non ha funzionato", fonts.title.get(), RectF(x, fx(124), cw, fx(26)),
                 argb(255, cError));
            text(g, jobError, fonts.text.get(), RectF(x, fx(160), cw, fx(120)), argb(255, cText), true);
            button(g, HPrimary, L"Riprova", right, by, true);
            button(g, HSecondary, L"Chiudi", hits[HPrimary].X - fx(10), by, false);
            break;
        case Page::UninstallAsk:
            text(g, L"Rimuovere MpvLight da questo computer?", fonts.title.get(),
                 RectF(x, fx(124), cw, fx(26)), argb(255, cText));
            text(g,
                 L"Verranno eliminati programma, motore, collegamenti e associazioni da " + opt.dir +
                     L". Gli screenshot salvati restano dove sono.",
                 fonts.text.get(), RectF(x, fx(158), cw, fx(66)), argb(255, cMuted), true);
            toggle(g, HSettings, L"Elimina anche impostazioni e file recenti", L"", opt.removeSettings, x,
                   fx(236), cw);
            button(g, HPrimary, L"Disinstalla", right, by, true);
            button(g, HSecondary, L"Annulla", hits[HPrimary].X - fx(10), by, false);
            break;
        case Page::UninstallDone:
            text(g, L"MpvLight è stato rimosso", fonts.title.get(), RectF(x, fx(124), cw, fx(26)),
                 argb(255, cGood));
            text(g, L"Grazie per averlo usato.", fonts.text.get(), RectF(x, fx(158), cw, fx(24)),
                 argb(255, cMuted));
            button(g, HPrimary, L"Chiudi", right, by, true);
            break;
        }
    }
    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(win, &ps);
}
void setPage(Page p) {
    page = p;
    hover = -1;
    placeEdit();
    InvalidateRect(win, nullptr, FALSE);
}
std::wstring readDirField() {
    int n = GetWindowTextLengthW(edit);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(edit, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    while (!s.empty() && (s.back() == L'\\' || s.back() == L' '))
        s.pop_back();
    while (!s.empty() && s.front() == L' ')
        s.erase(s.begin());
    return s;
}
void refreshEnginePresence() {
    enginePresent = exists(opt.dir + L"\\" + comp::kEngineDll);
    upgrade = exists(opt.dir + L"\\MpvLight.exe");
    opt.engine = !enginePresent || !opt.localEngine.empty();
}
void startJob(bool engineOnly = false) {
    if (page == Page::Options) {
        auto dir = readDirField();
        wchar_t full[32768];
        DWORD n = dir.empty() ? 0 : GetFullPathNameW(dir.c_str(), 32768, full, nullptr);
        if (!n || n >= 32768 || dir.size() < 3 || dir[1] != L':') {
            MessageBoxW(win,
                        L"Scrivi un percorso completo per la cartella, per esempio C:\\Programmi\\MpvLight.",
                        L"MpvLight Setup", MB_OK | MB_ICONWARNING);
            SetFocus(edit);
            return;
        }
        opt.dir.assign(full, n);
    }
    job = std::make_shared<Job>();
    jobText = L"Preparazione…";
    jobFraction = -1;
    engineError.clear();
    if (uninstallMode)
        std::thread(uninstallWorker, opt, job).detach();
    else
        std::thread(installWorker, opt, job, engineOnly).detach();
    setPage(Page::Working);
}
void pollJob() {
    if (!job)
        return;
    std::lock_guard<std::mutex> hold(job->lock);
    jobText = job->text;
    jobFraction = job->fraction;
    if (!job->finished)
        return;
    engineError = job->engineError;
    jobError = job->error;
    bool ok = job->ok;
    job.reset();
    if (uninstallMode)
        setPage(ok ? Page::UninstallDone : Page::Failed);
    else
        setPage(ok ? Page::Done : Page::Failed);
}
void browse() {
    IFileDialog *d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d))))
        return;
    DWORD flags{};
    d->GetOptions(&flags);
    d->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->SetTitle(L"Cartella di installazione di MpvLight");
    if (SUCCEEDED(d->Show(win))) {
        IShellItem *it = nullptr;
        if (SUCCEEDED(d->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                std::wstring dir = p;
                CoTaskMemFree(p);
                // A fresh empty pick gets its own MpvLight subfolder.
                if (!exists(dir + L"\\MpvLight.exe") && dir.size() > 3 &&
                    dir.substr(dir.find_last_of(L'\\') + 1) != L"MpvLight")
                    dir += L"\\MpvLight";
                SetWindowTextW(edit, dir.c_str());
                opt.dir = dir;
                refreshEnginePresence();
            }
            it->Release();
        }
    }
    d->Release();
    InvalidateRect(win, nullptr, FALSE);
}
bool pickLocalEngine() {
    wchar_t path[32768] = {};
    OPENFILENAMEW o{sizeof(o)};
    o.hwndOwner = win;
    o.lpstrFile = path;
    o.nMaxFile = 32768;
    o.lpstrFilter = L"Motore libmpv (*.7z, *.dll)\0*.7z;*.dll\0Tutti i file\0*.*\0";
    o.lpstrTitle = L"Scegli l'archivio mpv-dev-x86_64…7z oppure libmpv-2.dll";
    o.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&o))
        return false;
    opt.localEngine = path;
    opt.engine = true;
    return true;
}
void launchApp() {
    std::wstring exe = opt.dir + L"\\MpvLight.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, opt.dir.c_str(), SW_SHOWNORMAL);
}
// The temporary uninstaller copy removes itself a moment after exiting.
void scheduleSelfDelete() {
    if (tempSelf.empty())
        return;
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 >nul & del /f /q \"" + tempSelf + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                       &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}
void activate(int h) {
    switch (h) {
    case HPrimary:
        if (page == Page::Options || page == Page::UninstallAsk || page == Page::Failed)
            startJob();
        else if (page == Page::Done) {
            if (opt.launch)
                launchApp();
            DestroyWindow(win);
        } else if (page == Page::UninstallDone)
            DestroyWindow(win);
        break;
    case HSecondary:
        if (page == Page::Working) {
            if (job)
                job->cancel = true;
        } else
            DestroyWindow(win);
        break;
    case HBrowse:
        browse();
        break;
    case HStart:
        opt.startMenu = !opt.startMenu;
        break;
    case HDesktop:
        opt.desktop = !opt.desktop;
        break;
    case HAssoc:
        opt.associate = !opt.associate;
        break;
    case HEngine:
        opt.engine = !opt.engine;
        if (!opt.engine)
            opt.localEngine.clear();
        break;
    case HLaunch:
        opt.launch = !opt.launch;
        break;
    case HSettings:
        opt.removeSettings = !opt.removeSettings;
        break;
    case HLocal:
        if (pickLocalEngine() && page == Page::Done)
            startJob(true);
        break;
    case HRetry:
        opt.localEngine.clear();
        opt.engine = true;
        startJob(true);
        break;
    }
    InvalidateRect(win, nullptr, FALSE);
}
LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM a, LPARAM b) {
    switch (msg) {
    case WM_CREATE: {
        win = w;
        dpi = static_cast<int>(GetDpiForWindow(w));
        BOOL dark = TRUE;
        DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
        COLORREF caption = cBack;
        DwmSetWindowAttribute(w, 35, &caption, sizeof(caption)); // DWMWA_CAPTION_COLOR
        edit = CreateWindowExW(0, L"EDIT", opt.dir.c_str(), WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 1,
                               1, w, nullptr, inst, nullptr);
        SetWindowTheme(edit, L"DarkMode_CFD", nullptr);
        makeFonts();
        placeEdit();
        SetTimer(w, 1, 50, nullptr);
        return 0;
    }
    case WM_DPICHANGED: {
        dpi = HIWORD(a);
        makeFonts();
        auto r = reinterpret_cast<RECT *>(b);
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        placeEdit();
        return 0;
    }
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(a), cText);
        SetBkColor(reinterpret_cast<HDC>(a), cField);
        return reinterpret_cast<LRESULT>(fieldBrush);
    case WM_COMMAND:
        if (reinterpret_cast<HWND>(b) == edit && HIWORD(a) == EN_CHANGE) {
            opt.dir = readDirField();
            bool before = enginePresent, wasUpgrade = upgrade;
            refreshEnginePresence();
            if (before != enginePresent || wasUpgrade != upgrade)
                InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (page == Page::Working) {
            pollJob();
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        REAL x = static_cast<REAL>(GET_X_LPARAM(b)), y = static_cast<REAL>(GET_Y_LPARAM(b));
        int h = -1;
        for (int i = 0; i < HCount; ++i)
            if (hits[i].Width > 0 && hits[i].Contains(x, y))
                h = i;
        if (h != hover) {
            hover = h;
            InvalidateRect(w, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, w, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        hover = -1;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(b) == HTCLIENT && reinterpret_cast<HWND>(a) == w) {
            SetCursor(LoadCursorW(nullptr, hover >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP:
        if (hover >= 0)
            activate(hover);
        return 0;
    case WM_CLOSE:
        if (page == Page::Working) {
            if (job && !uninstallMode)
                job->cancel = true; // the worker stops and the Failed page appears
            return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, a, b);
}

// ---------------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------------
int runSilent() {
    auto j = std::make_shared<Job>();
    if (uninstallMode)
        uninstallWorker(opt, j);
    else
        installWorker(opt, j, false);
    // 0 = ok, 1 = failed, 2 = installed but the engine could not be downloaded
    return !j->ok ? 1 : j->engineError.empty() ? 0 : 2;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    inst = instance;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (lstrcmpiW(arg.c_str(), L"/uninstall") == 0)
            uninstallMode = true;
        else if (lstrcmpiW(arg.c_str(), L"/S") == 0)
            silent = true;
        else if (lstrcmpiW(arg.c_str(), L"/dir") == 0 && i + 1 < argc)
            opt.dir = argv[++i];
        else if (lstrcmpiW(arg.c_str(), L"/temp") == 0 && i + 1 < argc)
            tempSelf = argv[++i];
    }
    LocalFree(argv);
    if (opt.dir.empty())
        opt.dir = uninstallMode ? comp::regRead(L"InstallDir") : defaultDir();
    if (uninstallMode && opt.dir.empty()) {
        std::wstring me = selfPath();
        opt.dir = me.substr(0, me.find_last_of(L'\\'));
    }
    // The uninstaller cannot delete the folder it runs from: continue from a temporary copy.
    if (uninstallMode && tempSelf.empty()) {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        std::wstring copy =
            std::wstring(tmp) + L"MpvLight-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
        if (CopyFileW(selfPath().c_str(), copy.c_str(), FALSE)) {
            std::wstring cmd = L"\"" + copy + L"\" /uninstall /dir \"" + opt.dir + L"\" /temp \"" + copy +
                               L"\"" + (silent ? L" /S" : L"");
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                CoUninitialize();
                return 0;
            }
        }
    }
    // One setup at a time.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\MpvLightSetup");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"MpvLightSetup", nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        CloseHandle(single);
        CoUninitialize();
        return 0;
    }
    refreshEnginePresence();
    if (silent) {
        int rc = runSilent();
        scheduleSelfDelete();
        CoUninitialize();
        return rc;
    }

    GdiplusStartupInput gin;
    GdiplusStartup(&gdiplusToken, &gin, nullptr);
    fieldBrush = CreateSolidBrush(cField);
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&cc);
    HICON big{}, small_{};
    LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(1), GetSystemMetrics(SM_CXICON),
                          GetSystemMetrics(SM_CYICON), &big);
    LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(1), GetSystemMetrics(SM_CXSMICON),
                          GetSystemMetrics(SM_CYSMICON), &small_);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance;
    cls.lpfnWndProc = proc;
    cls.lpszClassName = L"MpvLightSetup";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = big;
    cls.hIconSm = small_;
    RegisterClassExW(&cls);
    page = uninstallMode ? Page::UninstallAsk : Page::Options;
    dpi = static_cast<int>(GetDpiForSystem());
    RECT r{0, 0, px(620), px(480)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, static_cast<UINT>(dpi));
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = r.right - r.left, h = r.bottom - r.top;
    HWND window = CreateWindowExW(
        0, L"MpvLightSetup", uninstallMode ? L"Disinstalla MpvLight" : L"Installa MpvLight", style,
        work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, w, h,
        nullptr, nullptr, instance, nullptr);
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
            activate(msg.wParam == VK_RETURN ? HPrimary : HSecondary);
            continue;
        }
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    fonts = Fonts{};
    if (headerIcon)
        DestroyIcon(headerIcon);
    if (editFont)
        DeleteObject(editFont);
    DeleteObject(fieldBrush);
    for (HICON i : {big, small_})
        if (i)
            DestroyIcon(i);
    GdiplusShutdown(gdiplusToken);
    scheduleSelfDelete();
    CoUninitialize();
    return 0;
}

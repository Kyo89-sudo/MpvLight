// MpvLight 0.5.2 — interfaccia Win32 in stile mpv: il video riempie la finestra e i controlli
// compaiono sopra il video in finestre "layered" semitrasparenti che si nascondono da sole.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "engine.hpp"
#include "playback.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdint>
#include <cstring>
#include <dwmapi.h>
// With NOMINMAX the GDI+ headers still expect min/max to be visible in their namespace.
namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus
#include <gdiplus.h>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <uxtheme.h>
#include <vector>
#include <windows.h>
#include <windowsx.h>
#include <mutex>
#include <thread>
// Last: the LZMA SDK headers it pulls in define the macros True/False.
#include "components.hpp"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

static const wchar_t *kVersion = L"0.5.2";

// ---------------------------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------------------------
constexpr COLORREF cWindow = RGB(13, 12, 18), cPanel = RGB(22, 21, 29), cPanel2 = RGB(31, 29, 40);
constexpr COLORREF cText = RGB(240, 238, 248), cMuted = RGB(160, 156, 180), cFaint = RGB(112, 108, 132);
constexpr COLORREF cViolet = RGB(139, 92, 246), cPink = RGB(236, 72, 153), cLilac = RGB(192, 132, 252);
constexpr COLORREF cError = RGB(248, 113, 113);

// Icon-font code points (Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on Windows 10).
namespace G {
constexpr wchar_t Play = 0xE768, Pause = 0xE769, Prev = 0xE892, Next = 0xE893, Mute = 0xE74F, Vol1 = 0xE993,
                  Vol2 = 0xE994, Vol3 = 0xE995, Audio = 0xE8D6, Subs = 0xE7F0, More = 0xE712, Full = 0xE740,
                  Window = 0xE73F, Video = 0xE714, Music = 0xEC4F, Close = 0xE711;
}

// ---------------------------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------------------------
enum Cmd : UINT {
    CmdOpen = 100,
    CmdPlay,
    CmdStop,
    CmdPrev,
    CmdNext,
    CmdGoTo,
    CmdMute,
    CmdFull,
    CmdOnTop,
    CmdAutoNext,
    CmdFitWindow,
    CmdShot,
    CmdShotNoSubs,
    CmdShotOpenLast,
    CmdShotOpenDir,
    CmdShotChooseDir,
    CmdShotPng,
    CmdShotJpg,
    CmdLoadSubs,
    CmdHelp,
    CmdAbout,
    CmdEngineUpdate,
    CmdClearRecent,
    CmdFrameStep,
    CmdFrameBack,
    CmdSpeedReset,
    CmdOpenFolder,
    CmdQuit,
    CmdAudioOff = 500,
    CmdVideoOff,
    CmdSubOff,
    CmdBarTimeout = 650,
    CmdSpeed = 600,
    CmdRecent = 700,
    CmdTrack = 2000
};
const double kSpeeds[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};

HINSTANCE appInstance{};
HWND mainWindow{}, host{}, barWnd{}, osdWnd{}, helpWnd{}, timeEdit{};
Engine engine;
PlaybackState playback, previous;
int dpi = 96;
ULONG_PTR gdiplusToken{};
std::wstring iconFamily = L"Segoe MDL2 Assets", semiFamily = L"Segoe UI Semibold";

// Settings
std::wstring base, ini, shots, shotFormat = L"png";
std::vector<std::wstring> recent;
bool autoNext = false, onTop = false, fitWindow = true;
// How long the control bar stays after the last mouse movement (0 = always visible).
const int kBarTimeouts[] = {2000, 3500, 6000, 0};
int barTimeout = 2000;
double startVolume = 75;

// Playlist (folder of the current file, or the files dropped together)
std::vector<std::wstring> playlist;
size_t playIndex = 0;
std::wstring currentPath;
bool recentPending = false, fitPending = false;

// Window / overlay state
bool full = false;
WINDOWPLACEMENT placement{sizeof(placement)};
float barAlpha = 0;
ULONGLONG lastMove = 0, lastTick = 0;
POINT lastCursor{-1, -1};
bool menuOpen = false, cursorHidden = false, showRemaining = false;
bool barDirty = true, osdDirty = true, hostDirty = true;
double pendingSeek = -1, previewTime = 0;
ULONGLONG pendingUntil = 0, lastScrubSeek = 0;
uint64_t seenError = 0, seenNotice = 0, seenCaptureError = 0;
int lastSecond = -1;
std::wstring osdText;
bool osdError = false;
// Values just set by the user win over the engine snapshot for a moment, so sliders do not jitter
// while the engine thread catches up.
struct Held {
    double volume{}, speed{};
    bool muted{};
    ULONGLONG volumeUntil{}, speedUntil{}, mutedUntil{};
} held;
ULONGLONG osdUntil = 0;

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------
int px(int n) {
    return MulDiv(n, dpi, 96);
}
float fx(float v) {
    return v * static_cast<float>(dpi) / 96.0f;
}
Color argb(int a, COLORREF c) {
    return Color(static_cast<BYTE>(std::clamp(a, 0, 255)), GetRValue(c), GetGValue(c), GetBValue(c));
}
// Deterministic per-pixel noise in [-1, 1] (triangular distribution). Added before rounding, it turns
// the visible steps of a dark, slow gradient (banding) into an imperceptible, static grain.
inline float ditherNoise(uint32_t x, uint32_t y) {
    auto mix = [](uint32_t v) {
        v ^= v >> 16;
        v *= 0x7feb352dU;
        v ^= v >> 15;
        v *= 0x846ca68bU;
        v ^= v >> 16;
        return v;
    };
    uint32_t h = mix(x * 0x9E3779B1U ^ mix(y + 0x632BE5ABU));
    return static_cast<float>(h & 0xFFFF) / 65535.0f + static_cast<float>(h >> 16) / 65535.0f - 1.0f;
}
void roundRect(GraphicsPath &path, const RectF &r, float radius) {
    float d = std::min(radius * 2, std::min(r.Width, r.Height));
    if (d <= 0.5f) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}
void fillRound(Graphics &g, const RectF &r, float radius, const Brush &b) {
    GraphicsPath path;
    roundRect(path, r, radius);
    g.FillPath(&b, &path);
}
void fillRound(Graphics &g, const RectF &r, float radius, Color c) {
    SolidBrush b(c);
    fillRound(g, r, radius, b);
}
std::wstring fileName(const std::wstring &path) {
    auto p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}
std::wstring folderOf(const std::wstring &path) {
    auto p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"" : path.substr(0, p);
}
std::wstring lower(std::wstring s) {
    CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}
std::wstring extension(const std::wstring &path) {
    auto name = fileName(path);
    auto p = name.find_last_of(L'.');
    return p == std::wstring::npos ? L"" : lower(name.substr(p + 1));
}
bool isAudioExt(const std::wstring &e) {
    for (auto x : {L"mp3", L"flac", L"wav", L"ogg", L"opus", L"m4a", L"aac", L"wma", L"ape", L"alac", L"mka"})
        if (e == x)
            return true;
    return false;
}
bool isVideoExt(const std::wstring &e) {
    for (auto x : {L"mkv", L"mp4", L"avi", L"webm", L"mov", L"m4v", L"ts", L"m2ts", L"mts", L"wmv", L"flv",
                   L"mpg", L"mpeg", L"vob", L"3gp", L"ogv", L"rmvb", L"divx", L"f4v"})
        if (e == x)
            return true;
    return false;
}
bool isSubExt(const std::wstring &e) {
    for (auto x : {L"srt", L"ass", L"ssa", L"vtt", L"sub", L"sup", L"idx"})
        if (e == x)
            return true;
    return false;
}
bool isMedia(const std::wstring &path) {
    auto e = extension(path);
    return isVideoExt(e) || isAudioExt(e);
}
bool exists(const std::wstring &path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
std::wstring speedText(double s) {
    wchar_t b[32];
    swprintf_s(b, L"%g×", std::round(s * 100) / 100);
    std::wstring r = b;
    std::replace(r.begin(), r.end(), L'.', L',');
    return r;
}
bool hasTrack(const char *type) {
    for (auto &t : playback.tracks)
        if (t.type == type)
            return true;
    return false;
}
int trackCount(const char *type) {
    int n = 0;
    for (auto &t : playback.tracks)
        n += t.type == type;
    return n;
}
bool trackSelected(const char *type) {
    for (auto &t : playback.tracks)
        if (t.type == type && t.selected)
            return true;
    return false;
}
bool hasVideoIn(const PlaybackState &s) {
    if (!s.loaded)
        return false;
    for (auto &t : s.tracks)
        if (t.type == "video" && t.selected)
            return true;
    return false;
}
bool hasVideo() {
    return hasVideoIn(playback);
}
bool canSeek() {
    return playback.loaded && playback.seekable && playback.duration > 0;
}
double displayTime() {
    return pendingSeek >= 0 ? pendingSeek : playback.time;
}
bool hasPrev() {
    return playlist.size() > 1 && playIndex > 0;
}
bool hasNext() {
    return playlist.size() > 1 && playIndex + 1 < playlist.size();
}

// ---------------------------------------------------------------------------------------------
// Fonts (GDI+)
// ---------------------------------------------------------------------------------------------
struct Fonts {
    std::unique_ptr<Font> icon, iconPlay, iconBig, text, semi, caption, smallSemi, title, hero, osd;
} fonts;
void makeFonts() {
    auto make = [](const std::wstring &family, float size) {
        return std::make_unique<Font>(family.c_str(), fx(size), FontStyleRegular, UnitPixel);
    };
    fonts.icon = make(iconFamily, 17);
    fonts.iconPlay = make(iconFamily, 21);
    fonts.iconBig = make(iconFamily, 56);
    fonts.text = make(L"Segoe UI", 14);
    fonts.semi = make(semiFamily, 13.5f);
    fonts.caption = make(L"Segoe UI", 12);
    fonts.smallSemi = make(semiFamily, 11.5f);
    fonts.title = make(semiFamily, 15);
    fonts.hero = make(semiFamily, 30);
    fonts.osd = make(L"Segoe UI", 15);
}
HICON winBigIcon{}, winSmallIcon{};
// Window/taskbar icons at the size the current DPI needs (a fixed 32 px icon gets blurry at 125-150%).
void applyWindowIcons(HWND w) {
    HICON big{}, small_{};
    UINT d = static_cast<UINT>(dpi);
    LoadIconWithScaleDown(appInstance, MAKEINTRESOURCEW(1), GetSystemMetricsForDpi(SM_CXICON, d),
                          GetSystemMetricsForDpi(SM_CYICON, d), &big);
    LoadIconWithScaleDown(appInstance, MAKEINTRESOURCEW(1), GetSystemMetricsForDpi(SM_CXSMICON, d),
                          GetSystemMetricsForDpi(SM_CYSMICON, d), &small_);
    if (big)
        SendMessageW(w, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (small_)
        SendMessageW(w, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_));
    if (big && winBigIcon)
        DestroyIcon(winBigIcon);
    if (small_ && winSmallIcon)
        DestroyIcon(winSmallIcon);
    if (big)
        winBigIcon = big;
    if (small_)
        winSmallIcon = small_;
}
void dropFonts() {
    fonts = Fonts{};
}
float measure(const std::wstring &s, const Font *f) {
    if (s.empty() || !f)
        return 0;
    Bitmap scratch(1, 1, PixelFormat32bppPARGB);
    Graphics g(&scratch);
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
    RectF out;
    g.MeasureString(s.c_str(), static_cast<INT>(s.size()), f, PointF(0, 0), &fmt, &out);
    return out.Width;
}
void drawText(Graphics &g, const std::wstring &s, const Font *f, const RectF &r, Color c,
              StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentCenter) {
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g.DrawString(s.c_str(), static_cast<INT>(s.size()), f, r, &fmt, &b);
}
void drawGlyph(Graphics &g, wchar_t ch, const Font *f, const RectF &r, Color c) {
    wchar_t s[2] = {ch, 0};
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    SolidBrush b(c);
    g.DrawString(s, 1, f, r, &fmt, &b);
}

// ---------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------
std::wstring readIni(const wchar_t *section, const wchar_t *key, const wchar_t *fallback) {
    std::wstring buffer(32768, L'\0');
    DWORD n = GetPrivateProfileStringW(section, key, fallback, buffer.data(), 32768, ini.c_str());
    buffer.resize(n);
    return buffer;
}
int readInt(const wchar_t *section, const wchar_t *key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.c_str()));
}
void writeIni(const wchar_t *section, const wchar_t *key, const std::wstring &value) {
    WritePrivateProfileStringW(section, key, value.c_str(), ini.c_str());
}
void saveRecent() {
    WritePrivateProfileStringW(L"Recent", nullptr, nullptr, ini.c_str());
    for (size_t i = 0; i < recent.size(); ++i)
        writeIni(L"Recent", (L"File" + std::to_wstring(i)).c_str(), recent[i]);
}
void addRecent(const std::wstring &path) {
    recent.erase(
        std::remove_if(recent.begin(), recent.end(),
                       [&](const std::wstring &p) { return lstrcmpiW(p.c_str(), path.c_str()) == 0; }),
        recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > 8)
        recent.resize(8);
    saveRecent();
    hostDirty = true;
}
void loadSettings() {
    for (int i = 0; i < 8; ++i) {
        auto p = readIni(L"Recent", (L"File" + std::to_wstring(i)).c_str(), L"");
        if (!p.empty())
            recent.push_back(p);
    }
    autoNext = readInt(L"Player", L"AutoNext", 0) != 0;
    onTop = readInt(L"Player", L"OnTop", 0) != 0;
    fitWindow = readInt(L"Player", L"FitWindow", 1) != 0;
    barTimeout = readInt(L"Player", L"BarTimeout", 2000);
    if (std::find(std::begin(kBarTimeouts), std::end(kBarTimeouts), barTimeout) == std::end(kBarTimeouts))
        barTimeout = 2000;
    startVolume = std::clamp(readInt(L"Player", L"Volume", 75), 0, 100);
}
void saveSettings() {
    writeIni(L"Player", L"AutoNext", autoNext ? L"1" : L"0");
    writeIni(L"Player", L"OnTop", onTop ? L"1" : L"0");
    writeIni(L"Player", L"FitWindow", fitWindow ? L"1" : L"0");
    writeIni(L"Player", L"BarTimeout", std::to_wstring(barTimeout));
    if (playback.ready)
        writeIni(L"Player", L"Volume", std::to_wstring(static_cast<int>(std::lround(playback.volume))));
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (full)
        wp = placement;
    else
        GetWindowPlacement(mainWindow, &wp);
    RECT r = wp.rcNormalPosition;
    writeIni(L"Window", L"X", std::to_wstring(r.left));
    writeIni(L"Window", L"Y", std::to_wstring(r.top));
    writeIni(L"Window", L"W", std::to_wstring(r.right - r.left));
    writeIni(L"Window", L"H", std::to_wstring(r.bottom - r.top));
    writeIni(L"Window", L"Max", wp.showCmd == SW_SHOWMAXIMIZED ? L"1" : L"0");
}

// ---------------------------------------------------------------------------------------------
// OSD messages
// ---------------------------------------------------------------------------------------------
void osd(std::wstring text, bool error = false) {
    osdText = std::move(text);
    osdError = error;
    osdUntil = GetTickCount64() + (error ? 6000 : 1800);
    osdDirty = true;
}
void info(const std::wstring &s) {
    MessageBoxW(mainWindow, s.c_str(), L"MpvLight", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------------------------
// Playback commands
// ---------------------------------------------------------------------------------------------
void poke() {
    lastMove = GetTickCount64();
    barDirty = true;
}
void buildFolderPlaylist(const std::wstring &file) {
    playlist.clear();
    playIndex = 0;
    auto dir = folderOf(file);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::wstring p = dir + L"\\" + fd.cFileName;
            if (isMedia(p))
                playlist.push_back(p);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    std::sort(playlist.begin(), playlist.end(), [](const std::wstring &a, const std::wstring &b) {
        return StrCmpLogicalW(fileName(a).c_str(), fileName(b).c_str()) < 0;
    });
    auto it = std::find_if(playlist.begin(), playlist.end(),
                           [&](const std::wstring &p) { return lstrcmpiW(p.c_str(), file.c_str()) == 0; });
    if (it == playlist.end()) {
        playlist.insert(playlist.begin(), file);
        playIndex = 0;
    } else
        playIndex = static_cast<size_t>(it - playlist.begin());
}
std::wstring fullPath(const std::wstring &p) {
    if (p.find(L"://") != std::wstring::npos)
        return p;
    wchar_t out[32768];
    DWORD n = GetFullPathNameW(p.c_str(), 32768, out, nullptr);
    return n && n < 32768 ? std::wstring(out, n) : p;
}
void loadFile(std::wstring path, bool keepPlaylist = false) {
    if (!playback.ready) {
        osd(L"Il motore non è ancora pronto", true);
        return;
    }
    path = fullPath(path);
    if (isSubExt(extension(path))) {
        if (playback.loaded) {
            engine.command("sub-add", utf8(path), "select");
            osd(L"Sottotitoli: " + fileName(path));
        } else
            osd(L"Apri prima un video, poi trascina i sottotitoli", true);
        return;
    }
    if (path.find(L"://") == std::wstring::npos && !exists(path)) {
        osd(L"File non trovato: " + fileName(path), true);
        recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
        saveRecent();
        hostDirty = true;
        return;
    }
    if (!keepPlaylist)
        buildFolderPlaylist(path);
    currentPath = path;
    recentPending = true;
    fitPending = true;
    pendingSeek = -1;
    engine.command("loadfile", utf8(path));
    SetFocus(mainWindow);
}
void loadMany(std::vector<std::wstring> files) {
    std::vector<std::wstring> media;
    for (auto &f : files) {
        f = fullPath(f);
        if (isMedia(f) || f.find(L"://") != std::wstring::npos)
            media.push_back(f);
        else if (isSubExt(extension(f)))
            loadFile(f);
    }
    if (media.empty())
        return;
    if (media.size() == 1) {
        loadFile(media[0]);
        return;
    }
    playlist = media;
    playIndex = 0;
    loadFile(media[0], true);
    osd(std::to_wstring(media.size()) + L" file in coda");
}
void playAt(size_t index) {
    if (index >= playlist.size())
        return;
    playIndex = index;
    loadFile(playlist[index], true);
    osd(std::to_wstring(index + 1) + L"/" + std::to_wstring(playlist.size()) + L"  " +
        fileName(playlist[index]));
}
void playPrev() {
    if (hasPrev())
        playAt(playIndex - 1);
}
void playNext() {
    if (hasNext())
        playAt(playIndex + 1);
}
void openFile(bool subtitle = false) {
    std::vector<wchar_t> buffer(65536, 0);
    OPENFILENAMEW o{sizeof(o)};
    o.hwndOwner = mainWindow;
    o.lpstrFile = buffer.data();
    o.nMaxFile = static_cast<DWORD>(buffer.size());
    o.lpstrFilter =
        subtitle
            ? L"Sottotitoli\0*.srt;*.ass;*.ssa;*.vtt;*.sub;*.sup;*.idx\0Tutti i file\0*.*\0"
            : L"Video e audio\0*.mkv;*.mp4;*.avi;*.webm;*.mov;*.m4v;*.ts;*.m2ts;*.mts;*.wmv;*.flv;*.mpg;*."
              L"mpeg;*.vob;*.3gp;*.ogv;*.mp3;*.flac;*.wav;*.ogg;*.opus;*.m4a;*.aac;*.wma;*.mka\0Video\0*."
              L"mkv;*.mp4;*.avi;*.webm;*.mov;*.m4v;*.ts;*.m2ts;*.wmv;*.flv;*.mpg;*.mpeg\0Audio\0*.mp3;*."
              L"flac;*.wav;*.ogg;*.opus;*.m4a;*.aac;*.wma;*.mka\0Tutti i file\0*.*\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | (subtitle ? 0 : OFN_ALLOWMULTISELECT);
    if (!currentPath.empty()) {
        static std::wstring dir;
        dir = folderOf(currentPath);
        o.lpstrInitialDir = dir.c_str();
    }
    if (!GetOpenFileNameW(&o))
        return;
    if (subtitle) {
        engine.command("sub-add", utf8(buffer.data()), "select");
        osd(L"Sottotitoli: " + fileName(buffer.data()));
        return;
    }
    // Multi-selection: directory, then file names, each NUL-terminated.
    std::wstring first = buffer.data();
    const wchar_t *p = buffer.data() + first.size() + 1;
    if (!*p) {
        loadFile(first);
        return;
    }
    std::vector<std::wstring> files;
    while (*p) {
        files.push_back(first + L"\\" + p);
        p += wcslen(p) + 1;
    }
    std::sort(files.begin(), files.end(), [](const std::wstring &a, const std::wstring &b) {
        return StrCmpLogicalW(fileName(a).c_str(), fileName(b).c_str()) < 0;
    });
    loadMany(files);
}
void requestSeek(double seconds, bool exact = true) {
    if (!canSeek()) {
        osd(L"Questo contenuto non consente lo spostamento nel tempo", true);
        return;
    }
    seconds = std::clamp(seconds, 0.0, playback.duration);
    engine.seek(seconds, playback.paused || playback.eof ? 1 : 0, exact);
    pendingSeek = seconds;
    pendingUntil = GetTickCount64() + 1200;
    barDirty = true;
}
void seekBy(double delta) {
    if (!canSeek())
        return;
    requestSeek(displayTime() + delta);
    osd(clockText(pendingSeek) + L" / " + clockText(playback.duration));
    poke();
}
void togglePlay() {
    if (!playback.loaded) {
        if (!currentPath.empty() && !playback.loading)
            loadFile(currentPath, true);
        else if (!playback.loading)
            openFile();
        return;
    }
    if (playback.eof) {
        requestSeek(0);
        engine.property("pause", "no");
    } else
        engine.command("cycle", "pause");
    poke();
}
void setVolume(double v, bool show = true) {
    v = std::clamp(v, 0.0, 100.0);
    engine.property("volume", std::to_string(static_cast<int>(std::lround(v))));
    if (playback.muted && v > 0)
        engine.property("mute", "no");
    playback.volume = held.volume = v;
    held.volumeUntil = GetTickCount64() + 400;
    if (v > 0) {
        playback.muted = held.muted = false;
        held.mutedUntil = held.volumeUntil;
    }
    if (show)
        osd(L"Volume " + std::to_wstring(static_cast<int>(std::lround(v))) + L"%");
    barDirty = true;
}
void toggleMute() {
    engine.command("cycle", "mute");
    osd(playback.muted ? L"Audio attivo" : L"Audio disattivato");
    playback.muted = held.muted = !playback.muted;
    held.mutedUntil = GetTickCount64() + 400;
    barDirty = true;
}
void setSpeed(double s) {
    s = std::clamp(s, 0.25, 4.0);
    char b[32];
    sprintf_s(b, "%.2f", s);
    engine.property("speed", b);
    playback.speed = held.speed = s;
    held.speedUntil = GetTickCount64() + 400;
    osd(L"Velocità " + speedText(s));
    barDirty = true;
}
void takeScreenshot(bool subtitles = true) {
    if (!playback.videoReady) {
        osd(L"Nessun fotogramma video disponibile", true);
        return;
    }
    if (playback.capturing)
        return;
    engine.capture(shots, utf8(shotFormat), subtitles);
}
void cycleTrack(const char *type) {
    std::vector<const Track *> list;
    for (auto &t : playback.tracks)
        if (t.type == type)
            list.push_back(&t);
    if (list.empty())
        return;
    int selected = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]->selected)
            selected = static_cast<int>(i);
    const char *prop = !strcmp(type, "audio") ? "aid" : !strcmp(type, "sub") ? "sid" : "vid";
    int next = selected + 1;
    if (next >= static_cast<int>(list.size())) {
        if (!strcmp(type, "sub")) {
            engine.property(prop, "no");
            osd(L"Sottotitoli disattivati");
            return;
        }
        next = 0;
    }
    const Track *chosen = list[static_cast<size_t>(next)];
    engine.property(prop, std::to_string(chosen->id));
    osd((!strcmp(type, "audio") ? L"Audio: " : L"Sottotitoli: ") + chosen->label);
}

// libmpv-2.dll: next to the exe first, then an explicit path from settings.ini ([Engine] Library=),
// then the usual install folder, so a freshly built portable\MpvLight.exe still finds the engine.
std::wstring findLibmpv() {
    std::vector<std::wstring> candidates;
    auto custom = readIni(L"Engine", L"Library", L"");
    if (!custom.empty())
        candidates.push_back(custom);
    candidates.push_back(base + L"\\libmpv-2.dll");
    candidates.push_back(base + L"\\mpv-2.dll");
    candidates.push_back(folderOf(base) + L"\\libmpv-2.dll");
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        candidates.push_back(std::wstring(local) + L"\\Programs\\MpvLight\\libmpv-2.dll");
        CoTaskMemFree(local);
    }
    for (auto &c : candidates)
        if (exists(c))
            return c;
    return base + L"\\libmpv-2.dll";
}

// ---------------------------------------------------------------------------------------------
// Window geometry
// ---------------------------------------------------------------------------------------------
RECT videoScreenRect() {
    RECT r{};
    GetClientRect(host, &r);
    MapWindowPoints(host, nullptr, reinterpret_cast<POINT *>(&r), 2);
    return r;
}
int barHeight() {
    return px(full ? 156 : 132);
}
bool mainShown() {
    return mainWindow && IsWindowVisible(mainWindow) && !IsIconic(mainWindow);
}
void applyOnTop() {
    if (!full)
        SetWindowPos(mainWindow, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void fullscreen() {
    full = !full;
    if (full) {
        GetWindowPlacement(mainWindow, &placement);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(mainWindow, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(mainWindow, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(mainWindow, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(mainWindow, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPlacement(mainWindow, &placement);
        SetWindowPos(mainWindow, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    }
    cursorHidden = false;
    poke();
    osdDirty = true;
    SetFocus(mainWindow);
}
// Resize the window so the video fills it at a comfortable size, like VLC/mpv: about 80% of the
// work area, scaling small videos up and large ones down, keeping the aspect ratio.
void fitToVideo() {
    if (full || IsZoomed(mainWindow) || IsIconic(mainWindow) || playback.videoW <= 0 || playback.videoH <= 0)
        return;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(mainWindow, MONITOR_DEFAULTTONEAREST), &mi);
    RECT work = mi.rcWork;
    double maxW = (work.right - work.left) * 0.8, maxH = (work.bottom - work.top) * 0.8;
    double scale = std::min(maxW / playback.videoW, maxH / playback.videoH);
    int cw = static_cast<int>(playback.videoW * scale), ch = static_cast<int>(playback.videoH * scale);
    RECT r{0, 0, cw, ch};
    AdjustWindowRectExForDpi(&r, static_cast<DWORD>(GetWindowLongPtrW(mainWindow, GWL_STYLE)), FALSE,
                             static_cast<DWORD>(GetWindowLongPtrW(mainWindow, GWL_EXSTYLE)),
                             static_cast<UINT>(dpi));
    int w = r.right - r.left, h = r.bottom - r.top;
    RECT now;
    GetWindowRect(mainWindow, &now);
    int x = (now.left + now.right) / 2 - w / 2, y = (now.top + now.bottom) / 2 - h / 2;
    x = std::clamp(x, static_cast<int>(work.left),
                   std::max(static_cast<int>(work.left), static_cast<int>(work.right) - w));
    y = std::clamp(y, static_cast<int>(work.top),
                   std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - h));
    SetWindowPos(mainWindow, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------------------------
// Layered surfaces (per-pixel alpha windows above the video)
// ---------------------------------------------------------------------------------------------
struct Surface {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ old{};
    void *bits{};
    int w{}, h{};
    void release() {
        if (dc) {
            SelectObject(dc, old);
            DeleteObject(bitmap);
            DeleteDC(dc);
        }
        dc = nullptr;
        bitmap = nullptr;
        bits = nullptr;
        w = h = 0;
    }
    bool ensure(int W, int H) {
        if (dc && W == w && H == h)
            return true;
        release();
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) {
            DeleteDC(dc);
            dc = nullptr;
            return false;
        }
        old = SelectObject(dc, bitmap);
        w = W;
        h = H;
        return true;
    }
    // `prefill` may write premultiplied BGRA pixels directly (used for the dithered scrim).
    template <class F>
    void present(HWND window, int x, int y, int W, int H, F draw,
                 void (*prefill)(uint32_t *, int, int) = nullptr) {
        if (W <= 0 || H <= 0 || !ensure(W, H))
            return;
        std::memset(bits, 0, static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
        if (prefill)
            prefill(static_cast<uint32_t *>(bits), W, H);
        {
            Bitmap canvas(W, H, W * 4, PixelFormat32bppPARGB, static_cast<BYTE *>(bits));
            Graphics g(&canvas);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            draw(g);
            g.Flush(FlushIntentionSync);
        }
        POINT dst{x, y}, src{0, 0};
        SIZE size{W, H};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(window, nullptr, &dst, &size, dc, &src, 0, &blend, ULW_ALPHA);
    }
} barSurface, osdSurface;

// ---------------------------------------------------------------------------------------------
// Control bar
// ---------------------------------------------------------------------------------------------
enum Part {
    PNone,
    PSeek,
    PPlay,
    PPrev,
    PNext,
    PMute,
    PVolume,
    PTime,
    PSpeed,
    PAudio,
    PSubs,
    PMenu,
    PFull,
    PBack,
    PCount
};
struct BarLayout {
    std::array<RectF, PCount> r{};
    std::array<bool, PCount> on{};
    float seekL{}, seekR{}, seekY{}, volL{}, volR{}, cy{};
};
Part hover = PNone, pressed = PNone;
ULONGLONG hoverSince = 0;
bool tipShown = false, scrubbing = false, volumeDrag = false, barTracking = false;
float hoverX = 0;
int barW = 0, barH = 0;

std::wstring timeText() {
    double t = scrubbing ? previewTime : displayTime();
    std::wstring s =
        showRemaining && playback.duration > 0 ? L"-" + clockText(playback.duration - t) : clockText(t);
    if (playback.duration > 0)
        s += L" / " + clockText(playback.duration);
    return s;
}
BarLayout barLayout(float W, float H) {
    BarLayout L;
    float pad = fx(16), b = fx(38);
    L.cy = H - fx(30);
    auto put = [&](Part p, float x, float w) {
        L.r[p] = RectF(x, L.cy - b / 2, w, b);
        L.on[p] = true;
    };
    float x = pad - fx(8);
    put(PPlay, x, b);
    x += b;
    put(PPrev, x, b);
    x += b;
    put(PNext, x, b);
    x += b + fx(6);
    put(PMute, x, b);
    x += b;
    if (W >= fx(600)) {
        L.r[PVolume] = RectF(x, L.cy - fx(12), fx(92), fx(24));
        L.on[PVolume] = true;
        L.volL = x + fx(4);
        L.volR = x + fx(86);
        x += fx(92) + fx(12);
    } else
        x += fx(6);
    float right = W - pad + fx(8);
    for (Part p : {PFull, PMenu, PSubs, PAudio}) {
        right -= b;
        put(p, right, b);
    }
    float sw = measure(speedText(playback.speed), fonts.smallSemi.get()) + fx(16);
    right -= sw + fx(6);
    L.r[PSpeed] = RectF(right, L.cy - fx(12), sw, fx(24));
    L.on[PSpeed] = true;
    float tw = measure(timeText(), fonts.semi.get()) + fx(10);
    if (x + tw < right - fx(6)) {
        L.r[PTime] = RectF(x, L.cy - fx(14), tw, fx(28));
        L.on[PTime] = true;
    }
    L.seekL = pad;
    L.seekR = W - pad;
    L.seekY = H - fx(68);
    L.r[PSeek] = RectF(pad - fx(6), L.seekY - fx(11), W - 2 * pad + fx(12), fx(22));
    L.on[PSeek] = true;
    return L;
}
Part hitBar(int x, int y) {
    if (barW <= 0)
        return PNone;
    auto L = barLayout(static_cast<float>(barW), static_cast<float>(barH));
    for (size_t p = PSeek; p < PBack; ++p)
        if (L.on[p] && L.r[p].Contains(static_cast<REAL>(x), static_cast<REAL>(y)))
            return static_cast<Part>(p);
    return PBack;
}
bool partEnabled(Part p) {
    switch (p) {
    case PSeek:
        return canSeek();
    case PPrev:
        return hasPrev();
    case PNext:
        return hasNext();
    case PAudio:
        return trackCount("audio") > 1;
    case PSubs:
        return playback.loaded;
    case PSpeed:
    case PPlay:
    case PTime:
        return playback.loaded;
    default:
        return playback.ready;
    }
}
const wchar_t *tipFor(Part p) {
    switch (p) {
    case PPlay:
        return playback.paused || playback.eof ? L"Riproduci (Spazio)" : L"Pausa (Spazio)";
    case PPrev:
        return L"File precedente (PagSu)";
    case PNext:
        return L"File successivo (PagGiù)";
    case PMute:
        return playback.muted ? L"Riattiva audio (M)" : L"Disattiva audio (M)";
    case PVolume:
        return L"Volume (↑ ↓, rotellina)";
    case PTime:
        return showRemaining ? L"Mostra tempo trascorso" : L"Mostra tempo rimanente";
    case PSpeed:
        return L"Velocità (+ −, Backspace azzera)";
    case PAudio:
        return trackCount("audio") > 1 ? L"Traccia audio" : L"Una sola traccia audio";
    case PSubs:
        return L"Sottotitoli (J cambia)";
    case PMenu:
        return L"Altre opzioni";
    case PFull:
        return full ? L"Esci da schermo intero (Esc)" : L"Schermo intero (F)";
    default:
        return nullptr;
    }
}
wchar_t volumeGlyph() {
    if (playback.muted || playback.volume <= 0.5)
        return G::Mute;
    return playback.volume < 34 ? G::Vol1 : playback.volume < 67 ? G::Vol2 : G::Vol3;
}
void bubble(Graphics &g, const std::wstring &s, float cx, float bottom, float W, float a) {
    float tw = measure(s, fonts.caption.get()), bw = tw + fx(18), bh = fx(26);
    float x = std::clamp(cx - bw / 2, fx(4), std::max(fx(4), W - bw - fx(4)));
    RectF r(x, bottom - bh, bw, bh);
    fillRound(g, r, fx(6), argb(static_cast<int>(238 * a), RGB(18, 17, 24)));
    drawText(g, s, fonts.caption.get(), r, argb(static_cast<int>(255 * a), cText), StringAlignmentCenter);
}
void drawBar(Graphics &g, float W, float H) {
    float a = barAlpha;
    auto A = [&](int v) { return static_cast<int>(static_cast<float>(v) * a + 0.5f); };
    auto L = barLayout(W, H);
    if (full && !playback.title.empty())
        drawText(g, playback.title, fonts.title.get(), RectF(fx(16), H - fx(128), W - fx(32), fx(24)),
                 argb(A(245), cText));
    // Seek bar
    bool seekable = canSeek();
    bool hot = seekable && (hover == PSeek || scrubbing);
    float th = hot ? fx(5) : fx(3), tl = L.seekL, tr = L.seekR, ty = L.seekY;
    fillRound(g, RectF(tl, ty - th / 2, tr - tl, th), th / 2,
              argb(A(seekable ? 72 : 40), RGB(255, 255, 255)));
    if (seekable) {
        double pos = scrubbing ? previewTime : displayTime();
        float frac = static_cast<float>(std::clamp(pos / playback.duration, 0.0, 1.0));
        float hx = std::clamp(hoverX, tl, tr);
        if (hot && !scrubbing && hx > tl + (tr - tl) * frac)
            fillRound(g, RectF(tl, ty - th / 2, hx - tl, th), th / 2, argb(A(60), RGB(255, 255, 255)));
        float pw = (tr - tl) * frac;
        if (pw > 0.5f) {
            LinearGradientBrush played(PointF(tl - 1, 0), PointF(tr + 1, 0), argb(A(255), cViolet),
                                       argb(A(255), cPink));
            fillRound(g, RectF(tl, ty - th / 2, pw, th), th / 2, played);
        }
        if (hot) {
            float kx = tl + pw, kr = fx(7);
            SolidBrush shade(Color(static_cast<BYTE>(A(90)), 0, 0, 0));
            g.FillEllipse(&shade, kx - kr, ty - kr + fx(1), kr * 2, kr * 2);
            SolidBrush knob(argb(A(255), RGB(255, 255, 255)));
            g.FillEllipse(&knob, kx - kr, ty - kr, kr * 2, kr * 2);
            double t = scrubbing ? previewTime : playback.duration * (hx - tl) / (tr - tl);
            bubble(g, clockText(t), scrubbing ? kx : hx, ty - fx(14), W, a);
        }
    }
    // Buttons
    for (Part p : {PPlay, PPrev, PNext, PMute, PAudio, PSubs, PMenu, PFull}) {
        if (!L.on[p])
            continue;
        const RectF &r = L.r[p];
        bool en = partEnabled(p);
        if (hover == p && en) {
            float rad = r.Width * 0.44f, cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
            SolidBrush hl(argb(A(pressed == p ? 60 : 34), RGB(255, 255, 255)));
            g.FillEllipse(&hl, cx - rad, cy - rad, rad * 2, rad * 2);
        }
        wchar_t ch = 0;
        switch (p) {
        case PPlay:
            ch = playback.loaded && !playback.paused && !playback.eof ? G::Pause : G::Play;
            break;
        case PPrev:
            ch = G::Prev;
            break;
        case PNext:
            ch = G::Next;
            break;
        case PMute:
            ch = volumeGlyph();
            break;
        case PAudio:
            ch = G::Audio;
            break;
        case PSubs:
            ch = G::Subs;
            break;
        case PMenu:
            ch = G::More;
            break;
        case PFull:
            ch = full ? G::Window : G::Full;
            break;
        default:
            break;
        }
        drawGlyph(g, ch, p == PPlay ? fonts.iconPlay.get() : fonts.icon.get(), r,
                  argb(A(en ? 246 : 92), RGB(255, 255, 255)));
        if (p == PSubs && trackSelected("sub")) {
            float cx = r.X + r.Width / 2;
            fillRound(g, RectF(cx - fx(7), r.GetBottom() - fx(8), fx(14), fx(2.5f)), fx(1.2f),
                      argb(A(255), cLilac));
        }
    }
    // Volume slider
    if (L.on[PVolume]) {
        float vl = L.volL, vr = L.volR, vy = L.cy, vh = fx(3);
        float frac = static_cast<float>(std::clamp(playback.volume / 100.0, 0.0, 1.0));
        fillRound(g, RectF(vl, vy - vh / 2, vr - vl, vh), vh / 2, argb(A(80), RGB(255, 255, 255)));
        if (frac > 0)
            fillRound(g, RectF(vl, vy - vh / 2, (vr - vl) * frac, vh), vh / 2,
                      argb(A(playback.muted ? 110 : 245), RGB(255, 255, 255)));
        if (hover == PVolume || volumeDrag) {
            float kx = vl + (vr - vl) * frac, kr = fx(6);
            SolidBrush knob(argb(A(255), RGB(255, 255, 255)));
            g.FillEllipse(&knob, kx - kr, vy - kr, kr * 2, kr * 2);
        }
    }
    // Time
    if (L.on[PTime]) {
        auto t = timeText();
        auto slash = t.find(L" / ");
        RectF r = L.r[PTime];
        if (hover == PTime)
            fillRound(g, r, fx(6), argb(A(26), RGB(255, 255, 255)));
        RectF inner(r.X + fx(5), r.Y, r.Width - fx(5), r.Height);
        if (slash == std::wstring::npos)
            drawText(g, t, fonts.semi.get(), inner, argb(A(246), cText));
        else {
            auto head = t.substr(0, slash);
            drawText(g, head, fonts.semi.get(), inner, argb(A(246), cText));
            float hw = measure(head, fonts.semi.get());
            RectF tail(inner.X + hw, inner.Y, inner.Width - hw + fx(4), inner.Height);
            drawText(g, t.substr(slash), fonts.semi.get(), tail, argb(A(175), RGB(200, 196, 214)));
        }
    }
    // Speed
    {
        RectF r = L.r[PSpeed];
        bool en = partEnabled(PSpeed), changed = std::abs(playback.speed - 1) > 0.01;
        if (hover == PSpeed && en)
            fillRound(g, r, fx(5), argb(A(34), RGB(255, 255, 255)));
        GraphicsPath path;
        roundRect(path, r, fx(5));
        Pen outline(changed ? argb(A(255), cLilac) : argb(A(en ? 120 : 60), RGB(255, 255, 255)), fx(1));
        g.DrawPath(&outline, &path);
        drawText(g, speedText(playback.speed), fonts.smallSemi.get(), r,
                 changed ? argb(A(255), cLilac) : argb(A(en ? 235 : 92), RGB(255, 255, 255)),
                 StringAlignmentCenter);
    }
    // Tooltip
    if (tipShown && hover != PNone && hover != PSeek && hover != PBack && pressed == PNone) {
        if (auto tip = tipFor(hover)) {
            const RectF &r = L.r[hover];
            bubble(g, tip, r.X + r.Width / 2, r.Y - fx(6), W, a);
        }
    }
}
// Dark scrim behind the controls: transparent at the top, 85% black at the bottom, with dithered
// alpha so the fade over the video shows no bands. Fully transparent pixels stay exactly 0, so the
// mouse still passes through the empty part of the window.
void fillScrim(uint32_t *pixels, int W, int H) {
    for (int y = 0; y < H; ++y) {
        float t = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
        float f = t < 0.4f ? 0.18f * t / 0.4f : 0.18f + 0.82f * (t - 0.4f) / 0.6f;
        float a = 215.0f * barAlpha * f;
        uint32_t *row = pixels + static_cast<size_t>(y) * static_cast<size_t>(W);
        if (a < 0.5f)
            continue;
        for (int x = 0; x < W; ++x) {
            int v =
                static_cast<int>(a + ditherNoise(static_cast<uint32_t>(x), static_cast<uint32_t>(y)) + 0.5f);
            row[x] = static_cast<uint32_t>(std::clamp(v, 1, 255)) << 24; // premultiplied black
        }
    }
}
void renderBar() {
    if (!barWnd || !host)
        return;
    RECT v = videoScreenRect();
    int W = v.right - v.left, H = std::min(barHeight(), static_cast<int>(v.bottom - v.top));
    if (W <= 0 || H <= 0)
        return;
    barW = W;
    barH = H;
    barSurface.present(
        barWnd, v.left, v.bottom - H, W, H,
        [&](Graphics &g) { drawBar(g, static_cast<float>(W), static_cast<float>(H)); }, fillScrim);
    barDirty = false;
}
void renderOsd() {
    if (!osdWnd)
        return;
    ULONGLONG now = GetTickCount64();
    float a = osdUntil > now ? std::min(1.0f, static_cast<float>(osdUntil - now) / 300.0f) : 0.0f;
    if (a <= 0 || osdText.empty()) {
        if (IsWindowVisible(osdWnd))
            ShowWindow(osdWnd, SW_HIDE);
        osdDirty = false;
        return;
    }
    RECT v = videoScreenRect();
    float maxW = static_cast<float>(v.right - v.left) - fx(40);
    float tw = std::min(measure(osdText, fonts.osd.get()), std::max(fx(60), maxW - fx(34)));
    int W = static_cast<int>(tw + fx(34)), H = px(40);
    osdSurface.present(osdWnd, v.left + px(18), v.top + px(18), W, H, [&](Graphics &g) {
        RectF r(0, 0, static_cast<REAL>(W), static_cast<REAL>(H));
        fillRound(g, r, fx(9), argb(static_cast<int>(222 * a), RGB(16, 15, 22)));
        fillRound(g, RectF(0, fx(8), fx(3), static_cast<REAL>(H) - fx(16)), fx(1.5f),
                  argb(static_cast<int>(255 * a), osdError ? cError : RGB(167, 139, 250)));
        drawText(g, osdText, fonts.osd.get(),
                 RectF(fx(16), 0, static_cast<REAL>(W) - fx(26), static_cast<REAL>(H)),
                 argb(static_cast<int>(255 * a), osdError ? RGB(254, 202, 202) : cText));
    });
    if (!IsWindowVisible(osdWnd) && mainShown())
        ShowWindow(osdWnd, SW_SHOWNOACTIVATE);
    osdDirty = false;
}

// ---------------------------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------------------------
void darkMenus() {
    // Undocumented but stable since Windows 10 1903: dark popup menus for the whole process.
    if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        using SetMode = int(WINAPI *)(int);
        using Flush = void(WINAPI *)();
        auto set = reinterpret_cast<SetMode>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
        auto flush = reinterpret_cast<Flush>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
        if (set)
            set(2);
        if (flush)
            flush();
    }
}
std::wstring menuEscape(std::wstring s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'&')
            out += L'&';
        out += c;
    }
    return out;
}
void item(HMENU m, UINT id, const std::wstring &label, bool enabled = true, bool checked = false,
          bool radio = false) {
    MENUITEMINFOW mi{sizeof(mi)};
    mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
    mi.fType = MFT_STRING | (radio ? MFT_RADIOCHECK : 0);
    mi.fState = (enabled ? MFS_ENABLED : MFS_DISABLED) | (checked ? MFS_CHECKED : 0);
    mi.wID = id;
    mi.dwTypeData = const_cast<wchar_t *>(label.c_str());
    InsertMenuItemW(m, static_cast<UINT>(GetMenuItemCount(m)), TRUE, &mi);
}
void separator(HMENU m) {
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
}
void submenu(HMENU m, HMENU child, const std::wstring &label, bool enabled = true) {
    AppendMenuW(m, MF_POPUP | (enabled ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(child), label.c_str());
}
std::vector<Track> menuTracks;
HMENU trackMenu(int group) {
    const char *types[] = {"audio", "video", "sub"};
    HMENU m = CreatePopupMenu();
    bool any = false;
    for (size_t i = 0; i < menuTracks.size(); ++i)
        if (menuTracks[i].type == types[group]) {
            item(m, CmdTrack + static_cast<UINT>(i), menuEscape(menuTracks[i].label), playback.loaded,
                 menuTracks[i].selected, true);
            any = true;
        }
    bool selected = trackSelected(types[group]);
    if (any)
        separator(m);
    item(m, CmdAudioOff + static_cast<UINT>(group), group == 2 ? L"Nessun sottotitolo" : L"Disattiva",
         playback.loaded && any, !selected && any, true);
    if (group == 0) {
        separator(m);
        item(m, CmdMute, L"Muto\tM", playback.ready, playback.muted);
    }
    if (group == 2) {
        separator(m);
        item(m, CmdLoadSubs, L"Carica sottotitoli da file…", playback.loaded);
    }
    return m;
}
HMENU speedMenu() {
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < static_cast<int>(std::size(kSpeeds)); ++i)
        item(m, CmdSpeed + static_cast<UINT>(i), speedText(kSpeeds[i]), playback.loaded,
             std::abs(playback.speed - kSpeeds[i]) < 0.01, true);
    separator(m);
    item(m, CmdSpeedReset, L"Velocità normale\tBackspace", playback.loaded);
    return m;
}
HMENU recentMenu() {
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < recent.size(); ++i)
        item(m, CmdRecent + static_cast<UINT>(i), menuEscape(fileName(recent[i])));
    if (!recent.empty())
        separator(m);
    item(m, CmdClearRecent, L"Svuota elenco", !recent.empty());
    return m;
}
HMENU fullMenu() {
    HMENU m = CreatePopupMenu();
    item(m, CmdOpen, L"Apri file…\tCtrl+O", playback.ready);
    submenu(m, recentMenu(), L"Apri recenti", !recent.empty());
    item(m, CmdOpenFolder, L"Mostra il file nella cartella", !currentPath.empty());
    separator(m);
    item(m, CmdPlay,
         playback.loaded && !playback.paused && !playback.eof ? L"Pausa\tSpazio" : L"Riproduci\tSpazio",
         playback.ready);
    item(m, CmdStop, L"Stop", playback.loaded);
    item(m, CmdPrev, L"File precedente\tPagSu", hasPrev());
    item(m, CmdNext, L"File successivo\tPagGiù", hasNext());
    item(m, CmdGoTo, L"Vai al tempo…\tCtrl+G", canSeek());
    item(m, CmdFrameStep, L"Fotogramma successivo\t.", playback.loaded);
    item(m, CmdFrameBack, L"Fotogramma precedente\t,", playback.loaded);
    submenu(m, speedMenu(), L"Velocità");
    item(m, CmdAutoNext, L"Passa da solo al file successivo", true, autoNext);
    separator(m);
    submenu(m, trackMenu(0), L"Audio");
    submenu(m, trackMenu(1), L"Video");
    submenu(m, trackMenu(2), L"Sottotitoli");
    separator(m);
    HMENU capture = CreatePopupMenu();
    bool can = playback.videoReady && !playback.capturing;
    item(capture, CmdShot, L"Cattura fotogramma\tS", can);
    item(capture, CmdShotNoSubs, L"Cattura senza sottotitoli\tShift+S", can);
    separator(capture);
    item(capture, CmdShotOpenLast, L"Apri ultima cattura", !playback.lastScreenshot.empty());
    item(capture, CmdShotOpenDir, L"Apri cartella screenshot");
    item(capture, CmdShotChooseDir, L"Scegli cartella…");
    separator(capture);
    item(capture, CmdShotPng, L"Formato PNG", true, shotFormat == L"png", true);
    item(capture, CmdShotJpg, L"Formato JPG", true, shotFormat == L"jpg", true);
    submenu(m, capture, L"Screenshot");
    HMENU view = CreatePopupMenu();
    item(view, CmdFull, full ? L"Esci da schermo intero\tF" : L"Schermo intero\tF");
    item(view, CmdOnTop, L"Sempre in primo piano\tCtrl+T", true, onTop);
    item(view, CmdFitWindow, L"Adatta la finestra al video", true, fitWindow);
    separator(view);
    const wchar_t *barLabels[] = {L"Nascondi i controlli dopo 2 secondi",
                                  L"Nascondi i controlli dopo 3,5 secondi",
                                  L"Nascondi i controlli dopo 6 secondi", L"Controlli sempre visibili"};
    for (UINT i = 0; i < 4; ++i)
        item(view, CmdBarTimeout + i, barLabels[i], true, barTimeout == kBarTimeouts[i], true);
    submenu(m, view, L"Finestra");
    separator(m);
    item(m, CmdHelp, L"Scorciatoie da tastiera\tF1");
    item(m, CmdAbout, L"Informazioni su MpvLight");
    item(m, CmdEngineUpdate, L"Aggiorna motore libmpv…");
    return m;
}
void runCommand(UINT id);
void showMenu(HMENU m, POINT at, bool above = false) {
    menuOpen = true;
    poke();
    SetForegroundWindow(mainWindow);
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | (above ? TPM_BOTTOMALIGN : TPM_TOPALIGN) | TPM_LEFTALIGN;
    UINT id = static_cast<UINT>(TrackPopupMenuEx(m, flags, at.x, at.y, mainWindow, nullptr));
    DestroyMenu(m);
    menuOpen = false;
    poke();
    if (id)
        runCommand(id);
    SetFocus(mainWindow);
}
void contextMenu(POINT screen) {
    menuTracks = playback.tracks;
    showMenu(fullMenu(), screen);
}
void barMenu(Part p) {
    menuTracks = playback.tracks;
    auto L = barLayout(static_cast<float>(barW), static_cast<float>(barH));
    RECT v = videoScreenRect();
    POINT at{v.left + static_cast<LONG>(L.r[p].X), v.bottom - barH + static_cast<LONG>(L.r[p].Y) - px(4)};
    HMENU m = p == PAudio ? trackMenu(0) : p == PSubs ? trackMenu(2) : p == PSpeed ? speedMenu() : fullMenu();
    showMenu(m, at, true);
}

// ---------------------------------------------------------------------------------------------
// Dialogs: go to time, screenshot folder, help/about
// ---------------------------------------------------------------------------------------------
HBRUSH panelBrush{}, controlBrush{};
INT_PTR CALLBACK timeDialogProc(HWND dialog, UINT msg, WPARAM a, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(dialog, 20, &dark, sizeof(dark));
        timeEdit = GetDlgItem(dialog, 122);
        SetWindowTheme(timeEdit, L"DarkMode_CFD", nullptr);
        SetWindowTheme(GetDlgItem(dialog, IDOK), L"DarkMode_Explorer", nullptr);
        SetWindowTheme(GetDlgItem(dialog, IDCANCEL), L"DarkMode_Explorer", nullptr);
        SendMessageW(timeEdit, EM_SETLIMITTEXT, 30, 0);
        SetWindowTextW(timeEdit, clockText(displayTime()).c_str());
        SetDlgItemTextW(dialog, 301, (L"Durata " + clockText(playback.duration)).c_str());
        SendMessageW(timeEdit, EM_SETSEL, 0, -1);
        SetFocus(timeEdit);
        return FALSE;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(panelBrush);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(a), cText);
        SetBkColor(reinterpret_cast<HDC>(a), msg == WM_CTLCOLOREDIT ? cPanel2 : cPanel);
        return reinterpret_cast<INT_PTR>(msg == WM_CTLCOLOREDIT ? controlBrush : panelBrush);
    case WM_COMMAND:
        if (LOWORD(a) == IDCANCEL) {
            EndDialog(dialog, 0);
            return TRUE;
        }
        if (LOWORD(a) == IDOK) {
            wchar_t value[64];
            GetWindowTextW(timeEdit, value, 64);
            auto seconds = parseTime(value);
            if (!seconds || *seconds > playback.duration) {
                SetDlgItemTextW(dialog, 301, L"Tempo non valido o oltre la durata.");
                SetFocus(timeEdit);
                SendMessageW(timeEdit, EM_SETSEL, 0, -1);
                return TRUE;
            }
            requestSeek(*seconds);
            EndDialog(dialog, 1);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
void goTime() {
    if (!canSeek())
        return;
    menuOpen = true;
    DialogBoxParamW(appInstance, MAKEINTRESOURCEW(2), mainWindow, timeDialogProc, 0);
    menuOpen = false;
    timeEdit = nullptr;
    SetFocus(mainWindow);
}
void chooseShotFolder() {
    IFileDialog *dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return;
    DWORD flags{};
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Cartella per gli screenshot");
    if (SUCCEEDED(dialog->Show(mainWindow))) {
        IShellItem *it = nullptr;
        if (SUCCEEDED(dialog->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                shots = p;
                CoTaskMemFree(p);
                writeIni(L"Screenshot", L"Directory", shots);
                engine.property("screenshot-directory", utf8(shots));
                osd(L"Cartella screenshot aggiornata");
            }
            it->Release();
        }
    }
    dialog->Release();
}

// Help / about window: a dark custom-drawn panel with two tabs.
int helpTab = 0, helpHover = -1;
RectF helpTabs[2], helpClose, helpLinks[3];
HICON helpIcon{};
const wchar_t *kShortcuts[][2] = {
    {L"Spazio  /  K", L"Riproduci o metti in pausa"},
    {L"←  →", L"Indietro / avanti 5 secondi"},
    {L"Shift + ←  →", L"Indietro / avanti 1 secondo"},
    {L"Ctrl + ←  →", L"Indietro / avanti 30 secondi"},
    {L"↑  ↓  /  rotellina", L"Volume"},
    {L"M", L"Muto"},
    {L"F  /  F11  /  doppio clic", L"Schermo intero (Esc per uscire)"},
    {L"PagSu  /  PagGiù", L"File precedente / successivo della cartella"},
    {L"+  −  /  Backspace", L"Velocità più alta / più bassa / normale"},
    {L".  ,", L"Fotogramma successivo / precedente (in pausa)"},
    {L"J  /  Shift + J", L"Cambia sottotitoli / traccia audio"},
    {L"S  /  Shift + S", L"Cattura il fotogramma, con o senza sottotitoli"},
    {L"Ctrl + O  /  Ctrl + G", L"Apri file / vai al tempo"},
    {L"Ctrl + T", L"Sempre in primo piano"},
    {L"Tasto destro  /  F1", L"Menu completo / questa guida"},
};
void paintHelp(HWND w) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(w, &ps);
    RECT rc;
    GetClientRect(w, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ old = SelectObject(dc, bmp);
    float W = static_cast<float>(rc.right), H = static_cast<float>(rc.bottom);
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        SolidBrush bg(argb(255, cPanel));
        g.FillRectangle(&bg, 0.0f, 0.0f, W, H);
        // Tabs
        const wchar_t *tabs[] = {L"Scorciatoie", L"Informazioni"};
        float x = fx(24);
        for (int i = 0; i < 2; ++i) {
            float tw = measure(tabs[i], fonts.title.get()) + fx(24);
            helpTabs[i] = RectF(x, fx(16), tw, fx(34));
            if (i == helpTab)
                fillRound(g, helpTabs[i], fx(8), argb(255, cPanel2));
            else if (helpHover == i)
                fillRound(g, helpTabs[i], fx(8), argb(255, RGB(27, 26, 35)));
            drawText(g, tabs[i], fonts.title.get(), helpTabs[i], argb(255, i == helpTab ? cText : cMuted),
                     StringAlignmentCenter);
            if (i == helpTab) {
                LinearGradientBrush line(PointF(helpTabs[i].X, 0), PointF(helpTabs[i].GetRight(), 0),
                                         argb(255, cViolet), argb(255, cPink));
                fillRound(g,
                          RectF(helpTabs[i].X + fx(12), helpTabs[i].GetBottom() - fx(3),
                                helpTabs[i].Width - fx(24), fx(2.5f)),
                          fx(1.2f), line);
            }
            x += tw + fx(6);
        }
        helpClose = RectF(W - fx(52), fx(16), fx(34), fx(34));
        if (helpHover == 9)
            fillRound(g, helpClose, fx(8), argb(255, cPanel2));
        drawGlyph(g, G::Close, fonts.caption.get(), helpClose, argb(255, cMuted));
        SolidBrush rule(argb(255, RGB(38, 36, 48)));
        g.FillRectangle(&rule, fx(24), fx(60), W - fx(48), fx(1));
        float y = fx(78);
        if (helpTab == 0) {
            for (auto &row : kShortcuts) {
                RectF key(fx(24), y + fx(2), measure(row[0], fonts.smallSemi.get()) + fx(18), fx(24));
                fillRound(g, key, fx(6), argb(255, cPanel2));
                drawText(g, row[0], fonts.smallSemi.get(), key, argb(255, cText), StringAlignmentCenter);
                drawText(g, row[1], fonts.text.get(), RectF(fx(250), y, W - fx(274), fx(28)),
                         argb(255, cMuted));
                y += fx(31);
            }
        } else {
            if (helpIcon) {
                HDC idc = g.GetHDC();
                DrawIconEx(idc, px(24), px(80), helpIcon, px(72), px(72), 0, nullptr, DI_NORMAL);
                g.ReleaseHDC(idc);
            }
            drawText(g, L"MpvLight", fonts.hero.get(), RectF(fx(112), fx(82), W - fx(136), fx(40)),
                     argb(255, cText));
            drawText(g, std::wstring(L"Versione ") + kVersion + L" · Windows x64", fonts.text.get(),
                     RectF(fx(114), fx(122), W - fx(136), fx(24)), argb(255, cMuted));
            y = fx(176);
            auto line = [&](const std::wstring &label, const std::wstring &value) {
                drawText(g, label, fonts.caption.get(), RectF(fx(24), y, fx(150), fx(24)), argb(255, cFaint));
                drawText(g, value.empty() ? L"—" : value, fonts.text.get(),
                         RectF(fx(174), y, W - fx(198), fx(24)), argb(255, cText));
                y += fx(30);
            };
            line(L"Motore", playback.version.empty() ? L"libmpv non caricata" : playback.version);
            line(L"Build", comp::installedBuild());
            line(L"Buffer in RAM", std::to_wstring(static_cast<int>(playback.bufferedSeconds)) +
                                      L" s disponibili · lettura anticipata fino a 20 s / 64 MiB");
            line(L"Decodifica", playback.loaded ? (playback.decoder.empty() || playback.decoder == L"no"
                                                       ? L"software (CPU)"
                                                       : L"hardware · " + playback.decoder)
                                                : L"automatica: GPU Intel, AMD o NVIDIA se supportata");
            line(L"Screenshot", shots);
            line(L"Impostazioni", ini);
            y += fx(6);
            drawText(
                g,
                L"Lettore leggero basato su libmpv. Interfaccia Win32 nativa, motore su un thread dedicato.",
                fonts.caption.get(), RectF(fx(24), y, W - fx(48), fx(22)), argb(255, cMuted));
            y += fx(22);
            drawText(
                g, L"Codice sotto licenza GPL-2.0-or-later. La DLL libmpv è una build di terzi (mpv/FFmpeg).",
                fonts.caption.get(), RectF(fx(24), y, W - fx(48), fx(22)), argb(255, cMuted));
            y += fx(36);
            const wchar_t *links[] = {L"MpvLight su GitHub", L"Build libmpv", L"Progetto mpv"};
            float lx = fx(24);
            for (int i = 0; i < 3; ++i) {
                float lw = measure(links[i], fonts.semi.get()) + fx(28);
                helpLinks[i] = RectF(lx, y, lw, fx(32));
                fillRound(g, helpLinks[i], fx(8), argb(255, helpHover == 10 + i ? RGB(44, 40, 58) : cPanel2));
                drawText(g, links[i], fonts.semi.get(), helpLinks[i], argb(255, cLilac),
                         StringAlignmentCenter);
                lx += lw + fx(10);
            }
        }
    }
    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(w, &ps);
}
LRESULT CALLBACK helpProc(HWND w, UINT msg, WPARAM a, LPARAM b) {
    switch (msg) {
    case WM_PAINT:
        paintHelp(w);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        REAL x = static_cast<REAL>(GET_X_LPARAM(b)), y = static_cast<REAL>(GET_Y_LPARAM(b));
        int h = helpTabs[0].Contains(x, y)                    ? 0
                : helpTabs[1].Contains(x, y)                  ? 1
                : helpClose.Contains(x, y)                    ? 9
                : helpTab == 1 && helpLinks[0].Contains(x, y) ? 10
                : helpTab == 1 && helpLinks[1].Contains(x, y) ? 11
                : helpTab == 1 && helpLinks[2].Contains(x, y) ? 12
                                                              : -1;
        if (h != helpHover) {
            helpHover = h;
            InvalidateRect(w, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, w, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        helpHover = -1;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, helpHover >= 0 ? IDC_HAND : IDC_ARROW));
        return TRUE;
    case WM_LBUTTONUP:
        if (helpHover == 0 || helpHover == 1) {
            helpTab = helpHover;
            InvalidateRect(w, nullptr, FALSE);
        } else if (helpHover == 9)
            PostMessageW(w, WM_CLOSE, 0, 0);
        else if (helpHover == 10)
            ShellExecuteW(w, L"open", L"https://github.com/Kyo89-sudo/MpvLight", nullptr,
                          nullptr, SW_SHOWNORMAL);
        else if (helpHover == 11)
            ShellExecuteW(w, L"open", L"https://github.com/shinchiro/mpv-winbuild-cmake/releases", nullptr,
                          nullptr, SW_SHOWNORMAL);
        else if (helpHover == 12)
            ShellExecuteW(w, L"open", L"https://mpv.io", nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    case WM_KEYDOWN:
        if (a == VK_ESCAPE || a == VK_F1)
            PostMessageW(w, WM_CLOSE, 0, 0);
        else if (a == VK_TAB || a == VK_LEFT || a == VK_RIGHT) {
            helpTab = 1 - helpTab;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE:
        // Re-enable the owner first, so Windows gives the activation back to it.
        EnableWindow(mainWindow, TRUE);
        DestroyWindow(w);
        SetForegroundWindow(mainWindow);
        return 0;
    case WM_DESTROY:
        helpWnd = nullptr;
        if (helpIcon)
            DestroyIcon(helpIcon);
        helpIcon = nullptr;
        return 0;
    }
    return DefWindowProcW(w, msg, a, b);
}
void showHelp(int tab) {
    helpTab = tab;
    if (helpWnd) {
        InvalidateRect(helpWnd, nullptr, FALSE);
        SetForegroundWindow(helpWnd);
        return;
    }
    LoadIconWithScaleDown(appInstance, MAKEINTRESOURCEW(1), px(72), px(72), &helpIcon);
    RECT r{0, 0, px(620), px(570)};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&r, style, FALSE, WS_EX_DLGMODALFRAME, static_cast<UINT>(dpi));
    RECT owner;
    GetWindowRect(mainWindow, &owner);
    int w = r.right - r.left, h = r.bottom - r.top;
    helpWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MpvLightHelp", L"Aiuto — MpvLight", style,
                              (owner.left + owner.right - w) / 2, (owner.top + owner.bottom - h) / 2, w, h,
                              mainWindow, nullptr, appInstance, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(helpWnd, 20, &dark, sizeof(dark));
    COLORREF caption = cPanel;
    DwmSetWindowAttribute(helpWnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    EnableWindow(mainWindow, FALSE);
    ShowWindow(helpWnd, SW_SHOW);
}

// ---------------------------------------------------------------------------------------------
// Engine update window: checks GitHub, downloads and installs libmpv-2.dll, offers a restart
// ---------------------------------------------------------------------------------------------
struct UpdateJob {
    std::mutex lock;
    std::atomic<bool> cancel{false};
    bool finished = false;
    comp::Report report;
    comp::Release release;
    bool releaseOk = false;
    std::wstring checkError;
    comp::EngineResult result;
};
enum class UpPhase { Checking, Ready, Working, Finished };
HWND updateWnd{};
UpPhase upPhase = UpPhase::Checking;
std::shared_ptr<UpdateJob> upJob;
comp::Asset upAsset;
bool upHasAsset = false;
std::wstring upMessage, upResultBuild;
bool upFailed = false;
comp::Report upReport;
int upHover = -1; // 0 primary, 1 secondary, 2 link
RectF upButtons[3];

void upStartCheck() {
    upPhase = UpPhase::Checking;
    upHasAsset = false;
    upFailed = false;
    upMessage = L"Ricerca dell'ultima versione su GitHub…";
    auto job = std::make_shared<UpdateJob>();
    upJob = job;
    std::thread([job] {
        comp::Release rel;
        std::wstring err;
        bool ok = comp::fetchLatest(rel, job->cancel, err);
        std::lock_guard<std::mutex> hold(job->lock);
        job->release = std::move(rel);
        job->releaseOk = ok;
        job->checkError = err;
        job->finished = true;
    }).detach();
}
void upStartInstall(const std::wstring &localFile) {
    upPhase = UpPhase::Working;
    upReport = comp::Report{comp::Stage::Download, -1, L"Preparazione…"};
    auto job = std::make_shared<UpdateJob>();
    upJob = job;
    comp::Asset asset = upHasAsset && localFile.empty() ? upAsset : comp::Asset{};
    std::wstring dir = base;
    std::thread([job, asset, localFile, dir] {
        auto result = comp::installEngine(dir, localFile, asset, comp::cpuSupportsV3(), job->cancel,
                                          [job](const comp::Report &r) {
                                              std::lock_guard<std::mutex> hold(job->lock);
                                              job->report = r;
                                          });
        std::lock_guard<std::mutex> hold(job->lock);
        job->result = std::move(result);
        job->finished = true;
    }).detach();
}
std::wstring upPickLocalFile() {
    wchar_t path[32768] = {};
    OPENFILENAMEW o{sizeof(o)};
    o.hwndOwner = updateWnd;
    o.lpstrFile = path;
    o.nMaxFile = 32768;
    o.lpstrFilter = L"Motore libmpv (*.7z, *.dll)\0*.7z;*.dll\0Tutti i file\0*.*\0";
    o.lpstrTitle = L"Scegli l'archivio mpv-dev-x86_64…7z oppure libmpv-2.dll";
    o.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&o) ? std::wstring(path) : std::wstring();
}
void upRestart() {
    wchar_t exe[32768];
    GetModuleFileNameW(nullptr, exe, 32768);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    if (!currentPath.empty())
        cmd += L" \"" + currentPath + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        PostMessageW(updateWnd, WM_CLOSE, 0, 0);
        PostMessageW(mainWindow, WM_CLOSE, 0, 0);
    }
}
// Poll the worker (50 ms timer).
void upPoll() {
    auto job = upJob;
    if (!job)
        return;
    std::lock_guard<std::mutex> hold(job->lock);
    if (upPhase == UpPhase::Checking && job->finished) {
        upPhase = UpPhase::Ready;
        if (!job->releaseOk) {
            upFailed = true;
            upMessage = L"Impossibile controllare gli aggiornamenti: " + job->checkError;
        } else if (auto a = comp::pickEngine(job->release, comp::cpuSupportsV3())) {
            upAsset = *a;
            upHasAsset = true;
            upMessage = comp::widen(a->name) + L"  ·  " + comp::megabytes(a->size);
        } else {
            upFailed = true;
            upMessage = L"La release " + comp::widen(job->release.tag) + L" non contiene la libreria x64";
        }
    } else if (upPhase == UpPhase::Working) {
        upReport = job->report;
        if (job->finished) {
            upPhase = UpPhase::Finished;
            upFailed = !job->result.ok;
            upResultBuild = job->result.build;
            upMessage = job->result.ok ? L"Motore installato: " + job->result.build
                                       : L"Aggiornamento non riuscito: " + job->result.error;
        }
    }
}
void paintUpdate(HWND w) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(w, &ps);
    RECT rc;
    GetClientRect(w, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ old = SelectObject(dc, bmp);
    float W = static_cast<float>(rc.right), H = static_cast<float>(rc.bottom);
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        SolidBrush bg(argb(255, cPanel));
        g.FillRectangle(&bg, 0.0f, 0.0f, W, H);
        float x = fx(24), y = fx(20), cw = W - fx(48);
        drawText(g, L"Motore libmpv", fonts.hero.get(), RectF(x, y, cw, fx(40)), argb(255, cText));
        y += fx(50);
        std::wstring installed = comp::installedBuild();
        if (installed.empty())
            installed = playback.ready ? playback.version : L"non installato";
        drawText(g, L"Installato", fonts.caption.get(), RectF(x, y, fx(110), fx(24)), argb(255, cFaint));
        drawText(g, installed, fonts.text.get(), RectF(x + fx(110), y, cw - fx(110), fx(24)),
                 argb(255, cText));
        y += fx(28);
        drawText(g, L"Disponibile", fonts.caption.get(), RectF(x, y, fx(110), fx(24)), argb(255, cFaint));
        std::wstring avail = upPhase == UpPhase::Checking ? L"ricerca in corso…"
                             : upHasAsset                 ? comp::widen(upAsset.name)
                                                          : L"—";
        drawText(g, avail, fonts.text.get(), RectF(x + fx(110), y, cw - fx(110), fx(24)), argb(255, cText));
        y += fx(40);
        // Status / progress
        if (upPhase == UpPhase::Working) {
            drawText(g, upReport.text, fonts.text.get(), RectF(x, y, cw, fx(24)), argb(255, cMuted));
            y += fx(32);
            RectF track(x, y, cw, fx(8));
            fillRound(g, track, fx(4), argb(255, cPanel2));
            float frac =
                upReport.fraction < 0 ? 0.35f : static_cast<float>(std::clamp(upReport.fraction, 0.0, 1.0));
            float start = 0;
            if (upReport.fraction < 0) // indeterminate: a sliding segment
                start = static_cast<float>((GetTickCount64() % 1400) / 1400.0) * (1 - frac);
            if (frac > 0) {
                RectF fill(x + cw * start, y, std::max(fx(8), cw * frac), fx(8));
                LinearGradientBrush brush(PointF(x - 1, 0), PointF(x + cw + 1, 0), argb(255, cViolet),
                                          argb(255, cPink));
                fillRound(g, fill, fx(4), brush);
            }
        } else {
            StringFormat wrap;
            wrap.SetLineAlignment(StringAlignmentNear);
            SolidBrush ink(argb(255, upFailed                       ? cError
                                     : upPhase == UpPhase::Finished ? RGB(134, 239, 172)
                                                                    : cMuted));
            std::wstring text = upMessage;
            if (upPhase == UpPhase::Ready && upHasAsset) {
                bool same = comp::installedBuild() == comp::widen(upAsset.name);
                text = same ? L"Hai già l'ultima versione. Puoi reinstallarla se il motore dà problemi."
                            : L"È disponibile una versione del motore da scaricare (" +
                                  comp::megabytes(upAsset.size) + L"). L'impronta SHA-256 verrà verificata.";
            }
            if (upPhase == UpPhase::Finished && !upFailed)
                text += L"\nRiavvia MpvLight per usare il nuovo motore.";
            g.DrawString(text.c_str(), static_cast<INT>(text.size()), fonts.text.get(),
                         RectF(x, y, cw, fx(66)), &wrap, &ink);
        }
        // Buttons
        const wchar_t *primary = nullptr, *secondary = L"Chiudi";
        bool link = false;
        switch (upPhase) {
        case UpPhase::Checking:
            secondary = L"Annulla";
            break;
        case UpPhase::Ready:
            if (upHasAsset)
                primary = !playback.ready                                       ? L"Installa"
                          : comp::installedBuild() == comp::widen(upAsset.name) ? L"Reinstalla"
                                                                                : L"Aggiorna";
            else
                primary = L"Riprova";
            link = true;
            break;
        case UpPhase::Working:
            secondary = L"Annulla";
            break;
        case UpPhase::Finished:
            primary = upFailed ? L"Riprova" : L"Riavvia MpvLight";
            secondary = upFailed ? L"Chiudi" : L"Più tardi";
            link = upFailed;
            break;
        }
        for (auto &b : upButtons)
            b = RectF();
        float by = H - fx(60), bh = fx(38), bx = W - fx(24);
        float sw = measure(secondary, fonts.title.get()) + fx(40);
        upButtons[1] = RectF(bx - sw, by, sw, bh);
        bx -= sw + fx(10);
        fillRound(g, upButtons[1], bh / 2, argb(255, upHover == 1 ? RGB(48, 45, 62) : cPanel2));
        drawText(g, secondary, fonts.title.get(), upButtons[1], argb(255, cText), StringAlignmentCenter);
        if (primary) {
            float pw = measure(primary, fonts.title.get()) + fx(48);
            upButtons[0] = RectF(bx - pw, by, pw, bh);
            LinearGradientBrush brush(PointF(upButtons[0].X - 1, 0), PointF(upButtons[0].GetRight() + 1, 0),
                                      argb(255, upHover == 0 ? RGB(147, 87, 255) : RGB(124, 58, 237)),
                                      argb(255, upHover == 0 ? RGB(240, 70, 150) : RGB(219, 39, 119)));
            fillRound(g, upButtons[0], bh / 2, brush);
            drawText(g, primary, fonts.title.get(), upButtons[0], argb(255, RGB(255, 255, 255)),
                     StringAlignmentCenter);
        }
        if (link) {
            const wchar_t *t = L"Usa un file già scaricato…";
            upButtons[2] = RectF(fx(24), by, measure(t, fonts.text.get()) + fx(4), bh);
            drawText(g, t, fonts.text.get(), upButtons[2],
                     argb(255, upHover == 2 ? RGB(216, 180, 254) : cLilac));
        }
    }
    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(w, &ps);
}
void upActivate(int which) {
    if (which == 1) {
        if (upPhase == UpPhase::Working || upPhase == UpPhase::Checking) {
            if (upJob)
                upJob->cancel = true;
            if (upPhase == UpPhase::Working)
                return; // the worker reports "annullata" and the window moves to Finished
        }
        PostMessageW(updateWnd, WM_CLOSE, 0, 0);
    } else if (which == 0) {
        if (upPhase == UpPhase::Ready)
            upHasAsset ? upStartInstall(L"") : upStartCheck();
        else if (upPhase == UpPhase::Finished)
            upFailed ? (upHasAsset ? upStartInstall(L"") : upStartCheck()) : upRestart();
    } else if (which == 2) {
        auto file = upPickLocalFile();
        if (!file.empty())
            upStartInstall(file);
    }
    InvalidateRect(updateWnd, nullptr, FALSE);
}
LRESULT CALLBACK updateProc(HWND w, UINT msg, WPARAM a, LPARAM b) {
    switch (msg) {
    case WM_PAINT:
        paintUpdate(w);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        upPoll();
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        REAL x = static_cast<REAL>(GET_X_LPARAM(b)), y = static_cast<REAL>(GET_Y_LPARAM(b));
        int h = -1;
        for (int i = 0; i < 3; ++i)
            if (upButtons[i].Width > 0 && upButtons[i].Contains(x, y))
                h = i;
        if (h != upHover) {
            upHover = h;
            InvalidateRect(w, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, w, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        upHover = -1;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, upHover >= 0 ? IDC_HAND : IDC_ARROW));
        return TRUE;
    case WM_LBUTTONUP:
        if (upHover >= 0)
            upActivate(upHover);
        return 0;
    case WM_KEYDOWN:
        if (a == VK_ESCAPE)
            upActivate(1);
        else if (a == VK_RETURN && upButtons[0].Width > 0)
            upActivate(0);
        return 0;
    case WM_CLOSE:
        if (upJob)
            upJob->cancel = true;
        KillTimer(w, 1);
        EnableWindow(mainWindow, TRUE);
        DestroyWindow(w);
        SetForegroundWindow(mainWindow);
        return 0;
    case WM_DESTROY:
        updateWnd = nullptr;
        upJob.reset();
        return 0;
    }
    return DefWindowProcW(w, msg, a, b);
}
void showEngineUpdate() {
    if (updateWnd) {
        SetForegroundWindow(updateWnd);
        return;
    }
    if (helpWnd)
        PostMessageW(helpWnd, WM_CLOSE, 0, 0);
    RECT r{0, 0, px(560), px(330)};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&r, style, FALSE, WS_EX_DLGMODALFRAME, static_cast<UINT>(dpi));
    RECT owner;
    GetWindowRect(mainWindow, &owner);
    int w = r.right - r.left, h = r.bottom - r.top;
    updateWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MpvLightUpdate", L"Aggiorna motore — MpvLight", style,
                                (owner.left + owner.right - w) / 2, (owner.top + owner.bottom - h) / 2, w, h,
                                mainWindow, nullptr, appInstance, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(updateWnd, 20, &dark, sizeof(dark));
    COLORREF caption = cPanel;
    DwmSetWindowAttribute(updateWnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    upHover = -1;
    upStartCheck();
    SetTimer(updateWnd, 1, 50, nullptr);
    EnableWindow(mainWindow, FALSE);
    ShowWindow(updateWnd, SW_SHOW);
}

// ---------------------------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------------------------
void runCommand(UINT id) {
    if (id >= CmdTrack && id < CmdTrack + menuTracks.size()) {
        const auto &t = menuTracks[id - CmdTrack];
        engine.property(t.type == "audio" ? "aid" : t.type == "video" ? "vid" : "sid", std::to_string(t.id));
        osd((t.type == "audio" ? L"Audio: " : t.type == "video" ? L"Video: " : L"Sottotitoli: ") + t.label);
        return;
    }
    if (id >= CmdBarTimeout && id < CmdBarTimeout + 4) {
        barTimeout = kBarTimeouts[id - CmdBarTimeout];
        const wchar_t *what[] = {L"I controlli si nascondono dopo 2 secondi",
                                 L"I controlli si nascondono dopo 3,5 secondi",
                                 L"I controlli si nascondono dopo 6 secondi", L"Controlli sempre visibili"};
        osd(what[id - CmdBarTimeout]);
        poke();
        return;
    }
    if (id >= CmdSpeed && id < CmdSpeed + std::size(kSpeeds)) {
        setSpeed(kSpeeds[id - CmdSpeed]);
        return;
    }
    if (id >= CmdRecent && id < CmdRecent + recent.size()) {
        loadFile(recent[id - CmdRecent]);
        return;
    }
    switch (id) {
    case CmdAudioOff:
    case CmdVideoOff:
    case CmdSubOff:
        engine.property(id == CmdAudioOff ? "aid" : id == CmdVideoOff ? "vid" : "sid", "no");
        osd(id == CmdSubOff     ? L"Sottotitoli disattivati"
            : id == CmdAudioOff ? L"Audio disattivato"
                                : L"Video disattivato");
        break;
    case CmdOpen:
        openFile();
        break;
    case CmdPlay:
        togglePlay();
        break;
    case CmdStop:
        engine.command("stop");
        break;
    case CmdPrev:
        playPrev();
        break;
    case CmdNext:
        playNext();
        break;
    case CmdGoTo:
        goTime();
        break;
    case CmdMute:
        toggleMute();
        break;
    case CmdFull:
        fullscreen();
        break;
    case CmdOnTop:
        onTop = !onTop;
        applyOnTop();
        osd(onTop ? L"Sempre in primo piano" : L"Primo piano disattivato");
        break;
    case CmdAutoNext:
        autoNext = !autoNext;
        osd(autoNext ? L"Passaggio automatico al file successivo" : L"Passaggio automatico disattivato");
        break;
    case CmdFitWindow:
        fitWindow = !fitWindow;
        if (fitWindow)
            fitToVideo();
        break;
    case CmdShot:
        takeScreenshot(true);
        break;
    case CmdShotNoSubs:
        takeScreenshot(false);
        break;
    case CmdShotOpenLast:
        if (!playback.lastScreenshot.empty())
            ShellExecuteW(mainWindow, L"open", playback.lastScreenshot.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        break;
    case CmdShotOpenDir:
        SHCreateDirectoryExW(nullptr, shots.c_str(), nullptr);
        ShellExecuteW(mainWindow, L"open", shots.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CmdShotChooseDir:
        chooseShotFolder();
        break;
    case CmdShotPng:
    case CmdShotJpg:
        shotFormat = id == CmdShotPng ? L"png" : L"jpg";
        writeIni(L"Screenshot", L"Format", shotFormat);
        engine.property("screenshot-format", utf8(shotFormat));
        osd(shotFormat == L"png" ? L"Formato screenshot PNG" : L"Formato screenshot JPG");
        break;
    case CmdLoadSubs:
        if (playback.loaded)
            openFile(true);
        break;
    case CmdHelp:
        showHelp(0);
        break;
    case CmdEngineUpdate:
        showEngineUpdate();
        break;
    case CmdAbout:
        showHelp(1);
        break;
    case CmdClearRecent:
        recent.clear();
        saveRecent();
        hostDirty = true;
        break;
    case CmdFrameStep:
        engine.command("frame-step");
        break;
    case CmdFrameBack:
        engine.command("frame-back-step");
        break;
    case CmdSpeedReset:
        setSpeed(1);
        break;
    case CmdOpenFolder:
        if (!currentPath.empty()) {
            std::wstring args = L"/select,\"" + currentPath + L"\"";
            ShellExecuteW(mainWindow, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case CmdQuit:
        PostMessageW(mainWindow, WM_CLOSE, 0, 0);
        break;
    }
    barDirty = hostDirty = true;
}

// ---------------------------------------------------------------------------------------------
// Bar window (mouse interaction)
// ---------------------------------------------------------------------------------------------
void scrubTo(int x, bool commit) {
    auto L = barLayout(static_cast<float>(barW), static_cast<float>(barH));
    previewTime = positionAt(x, static_cast<int>(L.seekL), static_cast<int>(L.seekR), playback.duration);
    hoverX = static_cast<float>(x);
    ULONGLONG now = GetTickCount64();
    if (commit)
        requestSeek(previewTime, true);
    else if (now - lastScrubSeek > 70) {
        engine.seek(previewTime, -1, false);
        lastScrubSeek = now;
    }
    barDirty = true;
}
void volumeTo(int x) {
    auto L = barLayout(static_cast<float>(barW), static_cast<float>(barH));
    setVolume(positionAt(x, static_cast<int>(L.volL), static_cast<int>(L.volR), 100.0), false);
}
void clickPart(Part p) {
    if (!partEnabled(p) && p != PMenu && p != PMute)
        return;
    switch (p) {
    case PPlay:
        togglePlay();
        break;
    case PPrev:
        playPrev();
        break;
    case PNext:
        playNext();
        break;
    case PMute:
        toggleMute();
        break;
    case PTime:
        showRemaining = !showRemaining;
        break;
    case PSpeed:
    case PAudio:
    case PSubs:
    case PMenu:
        barMenu(p);
        break;
    case PFull:
        fullscreen();
        break;
    default:
        break;
    }
    barDirty = true;
}
void wheelVolume(short delta) {
    setVolume(playback.volume + (delta > 0 ? 5 : -5));
    poke();
}
LRESULT CALLBACK layerProc(HWND w, UINT msg, WPARAM a, LPARAM b) {
    if (w != barWnd)
        return msg == WM_NCHITTEST ? HTTRANSPARENT : DefWindowProcW(w, msg, a, b);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(b), y = GET_Y_LPARAM(b);
        if (!barTracking) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, w, 0};
            TrackMouseEvent(&t);
            barTracking = true;
        }
        poke();
        if (scrubbing) {
            scrubTo(x, false);
            return 0;
        }
        if (volumeDrag) {
            volumeTo(x);
            return 0;
        }
        Part h = hitBar(x, y);
        if (h != hover) {
            hover = h;
            hoverSince = GetTickCount64();
            tipShown = false;
        }
        hoverX = static_cast<float>(x);
        barDirty = true;
        return 0;
    }
    case WM_MOUSELEAVE:
        barTracking = false;
        if (!scrubbing && !volumeDrag) {
            hover = PNone;
            tipShown = false;
            barDirty = true;
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        if (GetForegroundWindow() != mainWindow)
            SetForegroundWindow(mainWindow);
        int x = GET_X_LPARAM(b), y = GET_Y_LPARAM(b);
        Part p = hitBar(x, y);
        tipShown = false;
        if (p == PBack) {
            if (msg == WM_LBUTTONDBLCLK && playback.loaded)
                fullscreen();
            return 0;
        }
        pressed = p;
        if (p == PSeek && canSeek()) {
            scrubbing = true;
            SetCapture(w);
            scrubTo(x, false);
        } else if (p == PVolume && playback.ready) {
            volumeDrag = true;
            SetCapture(w);
            volumeTo(x);
        }
        barDirty = true;
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(b), y = GET_Y_LPARAM(b);
        if (scrubbing) {
            scrubbing = false;
            ReleaseCapture();
            scrubTo(x, true);
        } else if (volumeDrag) {
            volumeDrag = false;
            ReleaseCapture();
            osd(L"Volume " + std::to_wstring(static_cast<int>(std::lround(playback.volume))) + L"%");
        } else if (pressed != PNone && hitBar(x, y) == pressed) {
            Part p = pressed;
            pressed = PNone;
            clickPart(p);
        }
        pressed = PNone;
        barDirty = true;
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (scrubbing) {
            scrubbing = false;
            requestSeek(previewTime, true);
        }
        volumeDrag = false;
        pressed = PNone;
        barDirty = true;
        return 0;
    case WM_RBUTTONUP: {
        POINT p{GET_X_LPARAM(b), GET_Y_LPARAM(b)};
        ClientToScreen(w, &p);
        contextMenu(p);
        return 0;
    }
    case WM_MOUSEWHEEL:
        wheelVolume(GET_WHEEL_DELTA_WPARAM(a));
        return 0;
    case WM_DROPFILES:
        return SendMessageW(mainWindow, msg, a, b);
    }
    return DefWindowProcW(w, msg, a, b);
}

// ---------------------------------------------------------------------------------------------
// Host window: video area, welcome screen and audio-only view
// ---------------------------------------------------------------------------------------------
// Background of the start and audio-only screens: a soft radial glow from #1D1A28 at the top centre
// to #0D0C12 at the edges, dithered (a plain 8-bit gradient this dark has only ~16 steps and bands).
struct BackgroundCache {
    HBITMAP bitmap{};
    int w{}, h{};
} background;
void releaseBackground() {
    if (background.bitmap)
        DeleteObject(background.bitmap);
    background = BackgroundCache{};
}
void paintBackground(HDC dc, int W, int H) {
    if (!background.bitmap || background.w != W || background.h != H) {
        releaseBackground();
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        background.bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!background.bitmap) {
            RECT all{0, 0, W, H};
            HBRUSH b = CreateSolidBrush(cWindow);
            FillRect(dc, &all, b);
            DeleteObject(b);
            return;
        }
        background.w = W;
        background.h = H;
        const float c0[3] = {29, 26, 40}, c1[3] = {13, 12, 18};
        float cx = static_cast<float>(W) * 0.5f, cy = static_cast<float>(H) * 0.32f;
        float radius = std::max(1.0f, static_cast<float>(std::max(W, H)) * 0.7f);
        auto pixels = static_cast<uint32_t *>(bits);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float dx = static_cast<float>(x) - cx, dy = (static_cast<float>(y) - cy) * 1.25f;
                float t = std::min(1.0f, std::sqrt(dx * dx + dy * dy) / radius);
                t = t * t * (3 - 2 * t); // smoothstep: no visible edge where the glow ends
                float n = ditherNoise(static_cast<uint32_t>(x), static_cast<uint32_t>(y)) * 0.75f;
                uint32_t rgb = 0;
                for (int k = 0; k < 3; ++k) {
                    int v = static_cast<int>(c0[k] + (c1[k] - c0[k]) * t + n + 0.5f);
                    rgb = (rgb << 8) | static_cast<uint32_t>(std::clamp(v, 0, 255));
                }
                pixels[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)] = rgb;
            }
    }
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, background.bitmap);
    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}
RectF welcomeOpen;
std::vector<RectF> welcomeRows;
int welcomeHover = -2; // -1 = open button, >= 0 = recent row
HICON welcomeIcon{};
int welcomeIconSize = 0;
void ensureWelcomeIcon(int size) {
    if (welcomeIcon && welcomeIconSize == size)
        return;
    if (welcomeIcon)
        DestroyIcon(welcomeIcon);
    welcomeIcon = nullptr;
    LoadIconWithScaleDown(appInstance, MAKEINTRESOURCEW(1), size, size, &welcomeIcon);
    welcomeIconSize = size;
}
void paintHost(HWND w) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(w, &ps);
    RECT rc;
    GetClientRect(w, &rc);
    int Wi = std::max(1L, rc.right), Hi = std::max(1L, rc.bottom);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, Wi, Hi);
    HGDIOBJ old = SelectObject(dc, bmp);
    float W = static_cast<float>(Wi), H = static_cast<float>(Hi);
    welcomeRows.clear();
    welcomeOpen = RectF();
    if (playback.loaded && hasVideo()) {
        RECT all{0, 0, Wi, Hi};
        FillRect(dc, &all, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    } else {
        paintBackground(dc, Wi, Hi);
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        if (playback.loaded) {
            // Audio only
            float d = std::min(fx(170), H * 0.4f), cx = W / 2, cy = H / 2 - fx(50);
            LinearGradientBrush disc(PointF(cx - d / 2, cy - d / 2), PointF(cx + d / 2, cy + d / 2),
                                     argb(255, cViolet), argb(255, cPink));
            g.FillEllipse(&disc, cx - d / 2, cy - d / 2, d, d);
            drawGlyph(g, G::Music, fonts.iconBig.get(), RectF(cx - d / 2, cy - d / 2, d, d),
                      argb(255, RGB(255, 255, 255)));
            drawText(g, playback.title, fonts.hero.get(),
                     RectF(fx(24), cy + d / 2 + fx(20), W - fx(48), fx(40)), argb(255, cText),
                     StringAlignmentCenter);
            drawText(g, L"Solo audio", fonts.text.get(),
                     RectF(fx(24), cy + d / 2 + fx(62), W - fx(48), fx(24)), argb(255, cMuted),
                     StringAlignmentCenter);
        } else {
            int iconSize = px(112);
            size_t rows = std::min<size_t>(recent.size(), H > fx(620)   ? 6
                                                          : H > fx(520) ? 4
                                                          : H > fx(440) ? 2
                                                                        : 0);
            float total = fx(112 + 18 + 40 + 30 + 20 + 46 + 34) +
                          (rows ? fx(44) + fx(48) * static_cast<float>(rows) : 0.0f);
            float top = std::max(fx(20), (H - total) / 2);
            float cx = W / 2;
            ensureWelcomeIcon(iconSize);
            if (welcomeIcon) {
                HDC idc = g.GetHDC();
                DrawIconEx(idc, static_cast<int>(cx) - iconSize / 2, static_cast<int>(top), welcomeIcon,
                           iconSize, iconSize, 0, nullptr, DI_NORMAL);
                g.ReleaseHDC(idc);
            }
            float y = top + fx(112 + 14);
            drawText(g, L"MpvLight", fonts.hero.get(), RectF(0, y, W, fx(40)), argb(255, cText),
                     StringAlignmentCenter);
            y += fx(42);
            std::wstring sub = playback.loading ? L"Apertura in corso…"
                               : !playback.ready && !playback.error.empty()
                                   ? playback.error
                                   : L"Trascina qui un video o un file audio";
            bool bad = !playback.ready && !playback.error.empty();
            if (bad) {
                // Engine errors include the DLL path: let them wrap over a few lines.
                StringFormat wrap;
                wrap.SetAlignment(StringAlignmentCenter);
                wrap.SetLineAlignment(StringAlignmentNear);
                SolidBrush red(argb(255, cError));
                float ew = std::min(fx(640), W - fx(32));
                g.DrawString(sub.c_str(), static_cast<INT>(sub.size()), fonts.text.get(),
                             RectF(W / 2 - ew / 2, y, ew, fx(64)), &wrap, &red);
                y += fx(76);
            } else {
                drawText(g, sub, fonts.text.get(), RectF(fx(16), y, W - fx(32), fx(24)), argb(255, cMuted),
                         StringAlignmentCenter);
                y += fx(40);
            }
            float bw = fx(168), bh = fx(44);
            welcomeOpen = RectF(cx - bw / 2, y, bw, bh);
            {
                RectF glow = welcomeOpen;
                glow.Inflate(fx(3), fx(3));
                fillRound(g, RectF(glow.X, glow.Y + fx(5), glow.Width, glow.Height), bh / 2,
                          argb(40, RGB(168, 85, 247)));
                LinearGradientBrush btn(
                    PointF(welcomeOpen.X - 1, 0), PointF(welcomeOpen.GetRight() + 1, 0),
                    argb(255, welcomeHover == -1 ? RGB(147, 87, 255) : RGB(124, 58, 237)),
                    argb(255, welcomeHover == -1 ? RGB(240, 70, 150) : RGB(219, 39, 119)));
                fillRound(g, welcomeOpen, bh / 2, btn);
                drawText(g, bad ? L"Scarica il motore" : L"Apri file…", fonts.title.get(), welcomeOpen,
                         argb(255, RGB(255, 255, 255)), StringAlignmentCenter);
            }
            y += bh + fx(14);
            drawText(g, L"Ctrl+O apri · Spazio pausa · F schermo intero · F1 aiuto", fonts.caption.get(),
                     RectF(fx(16), y, W - fx(32), fx(20)), argb(255, cFaint), StringAlignmentCenter);
            y += fx(20);
            if (rows) {
                float lw = std::min(fx(540), W - fx(48)), lx = cx - lw / 2;
                y += fx(30);
                drawText(g, L"RECENTI", fonts.smallSemi.get(), RectF(lx + fx(12), y, lw, fx(18)),
                         argb(255, cFaint));
                y += fx(26);
                for (size_t i = 0; i < rows; ++i) {
                    RectF r(lx, y, lw, fx(46));
                    welcomeRows.push_back(r);
                    if (welcomeHover == static_cast<int>(i))
                        fillRound(g, r, fx(8), argb(16, RGB(255, 255, 255)));
                    bool audio = isAudioExt(extension(recent[i]));
                    drawGlyph(g, audio ? G::Music : G::Video, fonts.icon.get(),
                              RectF(r.X + fx(6), r.Y, fx(34), r.Height), argb(255, RGB(167, 139, 250)));
                    drawText(g, fileName(recent[i]), fonts.text.get(),
                             RectF(r.X + fx(46), r.Y + fx(4), r.Width - fx(58), fx(22)), argb(255, cText));
                    drawText(g, folderOf(recent[i]), fonts.caption.get(),
                             RectF(r.X + fx(46), r.Y + fx(24), r.Width - fx(58), fx(18)), argb(255, cFaint));
                    y += fx(48);
                }
            }
        }
    }
    BitBlt(target, 0, 0, Wi, Hi, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(w, &ps);
    hostDirty = false;
}
int welcomeHit(int x, int y) {
    if (playback.loaded)
        return -2;
    REAL fxp = static_cast<REAL>(x), fyp = static_cast<REAL>(y);
    if (welcomeOpen.Contains(fxp, fyp))
        return -1;
    for (size_t i = 0; i < welcomeRows.size(); ++i)
        if (welcomeRows[i].Contains(fxp, fyp))
            return static_cast<int>(i);
    return -2;
}
LRESULT CALLBACK hostProc(HWND w, UINT msg, WPARAM a, LPARAM b) {
    switch (msg) {
    case WM_PAINT:
        paintHost(w);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        hostDirty = true;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(b) == HTCLIENT) {
            SetCursor(cursorHidden ? nullptr
                                   : LoadCursorW(nullptr, welcomeHover >= -1 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        int h = welcomeHit(GET_X_LPARAM(b), GET_Y_LPARAM(b));
        if (h != welcomeHover) {
            welcomeHover = h;
            InvalidateRect(w, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, w, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (welcomeHover != -2) {
            welcomeHover = -2;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(mainWindow);
        return 0;
    case WM_LBUTTONUP: {
        int h = welcomeHit(GET_X_LPARAM(b), GET_Y_LPARAM(b));
        if (h == -1) {
            if (!playback.ready && !playback.error.empty())
                showEngineUpdate();
            else
                openFile();
        } else if (h >= 0 && h < static_cast<int>(recent.size()))
            loadFile(recent[static_cast<size_t>(h)]);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (playback.loaded)
            fullscreen();
        return 0;
    case WM_MBUTTONUP:
        if (playback.loaded)
            toggleMute();
        return 0;
    case WM_RBUTTONUP: {
        POINT p{GET_X_LPARAM(b), GET_Y_LPARAM(b)};
        ClientToScreen(w, &p);
        contextMenu(p);
        return 0;
    }
    case WM_CONTEXTMENU:
        return 0;
    case WM_MOUSEWHEEL:
        wheelVolume(GET_WHEEL_DELTA_WPARAM(a));
        return 0;
    }
    return DefWindowProcW(w, msg, a, b);
}

// ---------------------------------------------------------------------------------------------
// Periodic update (every 16 ms): engine snapshot, overlay visibility and animations
// ---------------------------------------------------------------------------------------------
void tick() {
    previous = playback;
    playback = engine.snapshot();
    ULONGLONG now = GetTickCount64();
    float dt = lastTick ? static_cast<float>(std::min<ULONGLONG>(now - lastTick, 100)) : 16.0f;
    lastTick = now;
    if (now < held.volumeUntil)
        playback.volume = held.volume;
    if (now < held.mutedUntil)
        playback.muted = held.muted;
    if (now < held.speedUntil)
        playback.speed = held.speed;

    if (pendingSeek >= 0 && (now > pendingUntil || std::abs(playback.time - pendingSeek) < 0.5))
        pendingSeek = -1;
    if (playback.errorSerial != seenError) {
        seenError = playback.errorSerial;
        osd(playback.error, true);
        hostDirty = true;
    }
    if (playback.noticeSerial != seenNotice) {
        seenNotice = playback.noticeSerial;
        auto n = playback.notice;
        if (n.rfind(L"Salvato: ", 0) == 0)
            n = L"Screenshot salvato · " + fileName(n.substr(9));
        osd(n);
    }
    if (playback.captureErrorSerial != seenCaptureError) {
        seenCaptureError = playback.captureErrorSerial;
        osd(L"Screenshot non salvato", true);
        PostMessageW(mainWindow, WM_APP + 1, 0, 0); // message box outside the timer tick
    }
    if (playback.loaded && !previous.loaded) {
        if (recentPending && !currentPath.empty()) {
            addRecent(currentPath);
            recentPending = false;
        }
        poke();
    }
    if (fitPending && playback.videoW > 0 && playback.videoH > 0) {
        fitPending = false;
        if (fitWindow)
            fitToVideo();
    }
    if (playback.loaded && fitPending && !playback.loading && trackCount("video") == 0)
        fitPending = false;
    if (playback.eof && !previous.eof && autoNext && hasNext())
        playNext();
    if (playback.ready != previous.ready || playback.loaded != previous.loaded ||
        playback.loading != previous.loading || hasVideo() != hasVideoIn(previous) ||
        playback.title != previous.title)
        hostDirty = true;
    std::wstring title = playback.loaded ? playback.title + L" — MpvLight" : L"MpvLight";
    wchar_t current[1024];
    GetWindowTextW(mainWindow, current, 1024);
    if (title != current)
        SetWindowTextW(mainWindow, title.c_str());

    // Bar redraw triggers
    int second = static_cast<int>(displayTime());
    if (second != lastSecond || playback.paused != previous.paused || playback.eof != previous.eof ||
        playback.volume != previous.volume || playback.muted != previous.muted ||
        playback.speed != previous.speed || playback.trackRevision != previous.trackRevision ||
        playback.duration != previous.duration) {
        lastSecond = second;
        barDirty = true;
    }
    if (!tipShown && hover > PSeek && hover < PBack && pressed == PNone && now - hoverSince > 550) {
        tipShown = true;
        barDirty = true;
    }

    // Visibility: show when the mouse moves over the video, while paused, for audio, or when used.
    POINT c;
    GetCursorPos(&c);
    RECT v = videoScreenRect();
    HWND under = WindowFromPoint(c);
    bool ours = under && GetAncestor(under, GA_ROOTOWNER) == mainWindow;
    bool inVideo = mainShown() && PtInRect(&v, c) && ours;
    if (c.x != lastCursor.x || c.y != lastCursor.y) {
        lastCursor = c;
        if (inVideo)
            lastMove = now;
    }
    bool overBar = inVideo && c.y >= v.bottom - barHeight();
    // The bar hides after `barTimeout` ms without mouse movement, also when paused and when the
    // pointer rests at the bottom. It stays while a control is under the pointer, during drags and
    // menus, at the end of the file and for audio-only files (nothing to look at).
    bool onControl = overBar && hover != PNone && hover != PBack && IsWindowVisible(barWnd);
    bool want = mainShown() && playback.loaded && !helpWnd && !updateWnd &&
                (barTimeout == 0 || playback.eof || !hasVideo() || onControl || scrubbing || volumeDrag ||
                 menuOpen || now - lastMove < static_cast<ULONGLONG>(barTimeout));
    float target = want ? 1.0f : 0.0f;
    if (barAlpha != target) {
        float step = dt / (want ? 140.0f : 280.0f);
        barAlpha = want ? std::min(1.0f, barAlpha + step) : std::max(0.0f, barAlpha - step);
        barDirty = true;
    }
    if (barAlpha > 0.001f && mainShown()) {
        if (barDirty)
            renderBar();
        if (!IsWindowVisible(barWnd))
            ShowWindow(barWnd, SW_SHOWNOACTIVATE);
    } else if (IsWindowVisible(barWnd)) {
        ShowWindow(barWnd, SW_HIDE);
        hover = PNone;
        tipShown = false;
    }
    // Cursor: hidden in fullscreen while the video plays and the mouse is still.
    bool hide = full && hasVideo() && !want && barAlpha == 0;
    if (hide != cursorHidden) {
        cursorHidden = hide;
        if (ours && under != barWnd)
            SetCursor(cursorHidden ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
    }
    // OSD fades out during its last 300 ms.
    if (!osdText.empty() && (osdDirty || (osdUntil > now && osdUntil - now < 320) ||
                             (osdUntil <= now && IsWindowVisible(osdWnd))))
        osdDirty = true;
    if (osdDirty)
        renderOsd();
    if (hostDirty)
        InvalidateRect(host, nullptr, FALSE);
}

// ---------------------------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------------------------
// Keep the overlay windows glued to the video area after a move or resize.
void placeLayers() {
    barDirty = osdDirty = true;
    if (IsWindowVisible(barWnd))
        renderBar();
    if (IsWindowVisible(osdWnd))
        renderOsd();
}
void layout() {
    RECT r;
    GetClientRect(mainWindow, &r);
    MoveWindow(host, 0, 0, r.right, r.bottom, TRUE);
    placeLayers();
}
LRESULT CALLBACK mainProc(HWND w, UINT msg, WPARAM a, LPARAM b) {
    switch (msg) {
    case WM_CREATE: {
        mainWindow = w;
        dpi = static_cast<int>(GetDpiForWindow(w));
        makeFonts();
        applyWindowIcons(w);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
        COLORREF caption = RGB(18, 17, 24), captionText = cText, border = RGB(44, 40, 58);
        DwmSetWindowAttribute(w, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(w, DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));
        DwmSetWindowAttribute(w, DWMWA_BORDER_COLOR, &border, sizeof(border));
        host = CreateWindowExW(0, L"MpvLightVideo", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 1, 1,
                               w, nullptr, appInstance, nullptr);
        barWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MpvLightLayer", L"",
                                 WS_POPUP, 0, 0, 1, 1, w, nullptr, appInstance, nullptr);
        osdWnd =
            CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            L"MpvLightLayer", L"", WS_POPUP, 0, 0, 1, 1, w, nullptr, appInstance, nullptr);
        DragAcceptFiles(w, TRUE);
        DragAcceptFiles(barWnd, TRUE);
        SetTimer(w, 1, 16, nullptr);
        return 0;
    }
    case WM_SIZE:
        if (a != SIZE_MINIMIZED)
            layout();
        return 0;
    case WM_MOVE:
        if (!IsIconic(w))
            placeLayers();
        return 0;
    case WM_DPICHANGED: {
        dpi = HIWORD(a);
        makeFonts();
        applyWindowIcons(w);
        auto r = reinterpret_cast<RECT *>(b);
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        hostDirty = true;
        layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto m = reinterpret_cast<MINMAXINFO *>(b);
        m->ptMinTrackSize = {px(520), px(340)};
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(w, &ps);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_TIMER:
        if (a == 1)
            tick();
        return 0;
    case WM_APP + 1:
        info(playback.captureError);
        return 0;
    case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(a);
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> files;
        for (UINT i = 0; i < n; ++i) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring p(static_cast<size_t>(len) + 1, L'\0');
            DragQueryFileW(drop, i, p.data(), len + 1);
            p.resize(len);
            files.push_back(p);
        }
        DragFinish(drop);
        if (!files.empty())
            SetForegroundWindow(w);
        loadMany(files);
        return 0;
    }
    case WM_APPCOMMAND:
        switch (GET_APPCOMMAND_LPARAM(b)) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE:
        case APPCOMMAND_MEDIA_PLAY:
        case APPCOMMAND_MEDIA_PAUSE:
            togglePlay();
            return TRUE;
        case APPCOMMAND_MEDIA_STOP:
            engine.command("stop");
            return TRUE;
        case APPCOMMAND_MEDIA_NEXTTRACK:
            playNext();
            return TRUE;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK:
            playPrev();
            return TRUE;
        }
        break;
    case WM_CONTEXTMENU: {
        POINT p{GET_X_LPARAM(b), GET_Y_LPARAM(b)};
        if (p.x == -1 && p.y == -1) {
            RECT v = videoScreenRect();
            p = {v.left + px(24), v.top + px(24)};
        }
        contextMenu(p);
        return 0;
    }
    case WM_CLOSE:
        KillTimer(w, 1);
        saveSettings();
        ShowWindow(barWnd, SW_HIDE);
        ShowWindow(osdWnd, SW_HIDE);
        ShowWindow(w, SW_HIDE);
        engine.stop();
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, a, b);
}

// Keyboard shortcuts, handled before dispatch so they work whatever child has focus.
bool handleKey(const MSG &msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)
        return false;
    HWND root = GetAncestor(msg.hwnd, GA_ROOT);
    if (root != mainWindow && msg.hwnd != mainWindow)
        return false;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0,
         alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    WPARAM k = msg.wParam;
    if (msg.message == WM_SYSKEYDOWN) {
        if (alt && k == VK_RETURN) {
            fullscreen();
            return true;
        }
        return false;
    }
    if (ctrl) {
        switch (k) {
        case 'O':
            openFile();
            return true;
        case 'G':
            goTime();
            return true;
        case 'S':
            takeScreenshot(true);
            return true;
        case 'T':
            runCommand(CmdOnTop);
            return true;
        case 'Q':
        case 'W':
            PostMessageW(mainWindow, WM_CLOSE, 0, 0);
            return true;
        case VK_LEFT:
            seekBy(-30);
            return true;
        case VK_RIGHT:
            seekBy(30);
            return true;
        }
        return false;
    }
    switch (k) {
    case VK_SPACE:
    case 'K':
    case 'P':
        togglePlay();
        return true;
    case VK_LEFT:
        seekBy(shift ? -1 : -5);
        return true;
    case VK_RIGHT:
        seekBy(shift ? 1 : 5);
        return true;
    case VK_UP:
        setVolume(playback.volume + 5);
        poke();
        return true;
    case VK_DOWN:
        setVolume(playback.volume - 5);
        poke();
        return true;
    case 'M':
        toggleMute();
        return true;
    case 'F':
    case VK_F11:
        fullscreen();
        return true;
    case VK_ESCAPE:
        if (full)
            fullscreen();
        return true;
    case VK_F1:
        showHelp(0);
        return true;
    case VK_PRIOR:
        playPrev();
        return true;
    case VK_NEXT:
        playNext();
        return true;
    case VK_HOME:
        if (canSeek())
            requestSeek(0);
        return true;
    case VK_OEM_PLUS:
    case VK_ADD:
        if (playback.loaded)
            setSpeed(playback.speed + 0.25);
        return true;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        if (playback.loaded)
            setSpeed(playback.speed - 0.25);
        return true;
    case VK_BACK:
        if (playback.loaded)
            setSpeed(1);
        return true;
    case VK_OEM_PERIOD:
        if (playback.loaded)
            engine.command("frame-step");
        return true;
    case VK_OEM_COMMA:
        if (playback.loaded)
            engine.command("frame-back-step");
        return true;
    case 'J':
        cycleTrack(shift ? "audio" : "sub");
        return true;
    case 'S':
        takeScreenshot(!shift);
        return true;
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    appInstance = instance;
    GdiplusStartupInput gdiplusInput;
    GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    {
        FontFamily fluent(L"Segoe Fluent Icons");
        if (fluent.IsAvailable())
            iconFamily = L"Segoe Fluent Icons";
        FontFamily semi(L"Segoe UI Semibold");
        if (!semi.IsAvailable())
            semiFamily = L"Segoe UI";
    }
    darkMenus();
    panelBrush = CreateSolidBrush(cPanel);
    controlBrush = CreateSolidBrush(cPanel2);

    // Settings location: next to the exe when portable.flag exists, otherwise %LOCALAPPDATA%.
    wchar_t path[32768];
    GetModuleFileNameW(nullptr, path, 32768);
    base = folderOf(path);
    bool portable = exists(base + L"\\portable.flag");
    if (portable)
        ini = base + L"\\settings.ini";
    else {
        PWSTR local = nullptr;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
        std::wstring dir = local ? local : base;
        CoTaskMemFree(local);
        dir += L"\\MpvLight";
        CreateDirectoryW(dir.c_str(), nullptr);
        ini = dir + L"\\settings.ini";
    }
    std::wstring defaultShots = folderOf(ini) + L"\\Screenshots";
    if (!portable) {
        PWSTR pictures = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures))) {
            defaultShots = std::wstring(pictures) + L"\\MpvLight";
            CoTaskMemFree(pictures);
        }
    }
    shots = readIni(L"Screenshot", L"Directory", defaultShots.c_str());
    shotFormat = readIni(L"Screenshot", L"Format", L"png") == L"jpg" ? L"jpg" : L"png";
    loadSettings();

    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&cc);
    HICON bigIcon =
        static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON), 0));
    HICON smallIcon =
        static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpfnWndProc = mainProc;
    cls.lpszClassName = L"MpvLightWindow";
    cls.hIcon = bigIcon;
    cls.hIconSm = smallIcon;
    RegisterClassExW(&cls);
    cls.lpfnWndProc = hostProc;
    cls.lpszClassName = L"MpvLightVideo";
    cls.style = CS_DBLCLKS;
    cls.hIcon = cls.hIconSm = nullptr;
    RegisterClassExW(&cls);
    cls.lpfnWndProc = layerProc;
    cls.lpszClassName = L"MpvLightLayer";
    RegisterClassExW(&cls);
    cls.lpfnWndProc = updateProc;
    cls.lpszClassName = L"MpvLightUpdate";
    cls.style = 0;
    RegisterClassExW(&cls);
    cls.lpfnWndProc = helpProc;
    cls.lpszClassName = L"MpvLightHelp";
    cls.style = 0;
    cls.hIcon = bigIcon;
    cls.hIconSm = smallIcon;
    RegisterClassExW(&cls);

    // Initial size: the saved one, or 70% of the work area in 16:9.
    dpi = static_cast<int>(GetDpiForSystem());
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int ww = (work.right - work.left) * 7 / 10, wh = ww * 9 / 16 + px(32);
    if (wh > (work.bottom - work.top) * 8 / 10) {
        wh = (work.bottom - work.top) * 8 / 10;
        ww = (wh - px(32)) * 16 / 9;
    }
    int wx = work.left + (work.right - work.left - ww) / 2, wy = work.top + (work.bottom - work.top - wh) / 2;
    HWND w = CreateWindowExW(0, L"MpvLightWindow", L"MpvLight", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy,
                             ww, wh, nullptr, nullptr, instance, nullptr);
    // Restore the last size and position (workspace coordinates, as GetWindowPlacement saved them).
    int sw = readInt(L"Window", L"W", 0), sh = readInt(L"Window", L"H", 0);
    RECT saved{readInt(L"Window", L"X", wx), readInt(L"Window", L"Y", wy), 0, 0};
    saved.right = saved.left + sw;
    saved.bottom = saved.top + sh;
    bool startMax = readInt(L"Window", L"Max", 0) != 0 && (show == SW_SHOWNORMAL || show == SW_SHOWDEFAULT);
    if (sw >= px(400) && sh >= px(260) && MonitorFromRect(&saved, MONITOR_DEFAULTTONULL)) {
        WINDOWPLACEMENT wp{sizeof(wp)};
        wp.rcNormalPosition = saved;
        wp.showCmd = startMax ? SW_SHOWMAXIMIZED : static_cast<UINT>(show);
        SetWindowPlacement(w, &wp);
    } else
        ShowWindow(w, startMax ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(w);
    applyOnTop();
    comp::removeStale(base);
    engine.start(findLibmpv(), host, shots, utf8(shotFormat));
    engine.property("volume", std::to_string(static_cast<int>(startVolume)));

    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> startup;
    for (int i = 1; i < argc; ++i)
        startup.push_back(argv[i]);
    LocalFree(argv);
    bool startupSent = startup.empty();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!startupSent && engine.snapshot().ready) {
            playback = engine.snapshot();
            loadMany(startup);
            startupSent = true;
        }
        HWND modal = helpWnd ? helpWnd : updateWnd;
        if (modal && (msg.message == WM_KEYDOWN) && GetAncestor(msg.hwnd, GA_ROOT) == modal) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        if (handleKey(msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    engine.stop();
    barSurface.release();
    osdSurface.release();
    if (welcomeIcon)
        DestroyIcon(welcomeIcon);
    releaseBackground();
    dropFonts();
    DeleteObject(panelBrush);
    DeleteObject(controlBrush);
    for (HICON i : {winBigIcon, winSmallIcon})
        if (i)
            DestroyIcon(i);
    if (bigIcon)
        DestroyIcon(bigIcon);
    if (smallIcon)
        DestroyIcon(smallIcon);
    GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}

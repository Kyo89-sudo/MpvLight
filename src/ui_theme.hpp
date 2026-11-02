#pragma once
#include <windows.h>

enum class ThemeMode { Dark, Light, System };

inline ThemeMode themeModeFromSetting(int value) {
    return value == 1 ? ThemeMode::Light : value == 2 ? ThemeMode::System : ThemeMode::Dark;
}

struct ThemeColors {
    COLORREF window, panel, panel2, text, muted, faint;
    COLORREF violet, pink, lilac, error, hover, border, success, glow;
};

inline ThemeColors themeColors(bool dark) {
    if (!dark)
        return {RGB(245, 244, 250), RGB(255, 255, 255), RGB(232, 229, 241),
                RGB(30, 25, 44), RGB(80, 72, 100), RGB(105, 96, 124),
                RGB(109, 40, 217), RGB(190, 24, 93), RGB(109, 40, 217),
                RGB(185, 28, 28), RGB(220, 213, 238), RGB(204, 197, 218),
                RGB(21, 128, 61), RGB(236, 226, 252)};
    return {RGB(13, 12, 18), RGB(22, 21, 29), RGB(31, 29, 40),
            RGB(240, 238, 248), RGB(160, 156, 180), RGB(144, 140, 164),
            RGB(139, 92, 246), RGB(236, 72, 153), RGB(192, 132, 252),
            RGB(248, 113, 113), RGB(44, 40, 58), RGB(38, 36, 48),
            RGB(134, 239, 172), RGB(29, 26, 40)};
}

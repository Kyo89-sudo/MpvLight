#define NOMINMAX
#include "../src/engine_queue.hpp"
#include "../src/ui_theme.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
double luminance(COLORREF color) {
    auto linear = [](BYTE value) {
        double s = value / 255.0;
        return s <= .04045 ? s / 12.92 : std::pow((s + .055) / 1.055, 2.4);
    };
    return .2126 * linear(GetRValue(color)) + .7152 * linear(GetGValue(color)) +
           .0722 * linear(GetBValue(color));
}
int main() {
    try {
        require(themeModeFromSetting(-1) == ThemeMode::Dark, "invalid theme fallback");
        require(themeModeFromSetting(99) == ThemeMode::Dark, "out-of-range theme fallback");
        require(themeModeFromSetting(1) == ThemeMode::Light, "light theme setting");
        require(themeModeFromSetting(2) == ThemeMode::System, "system theme setting");
        for (bool dark : {false, true}) {
            const auto c = themeColors(dark);
            for (auto foreground : {c.text, c.muted, c.faint})
                for (auto background : {c.window, c.panel, c.panel2}) {
                    double a = luminance(foreground), b = luminance(background);
                    require((std::max(a, b) + .05) / (std::min(a, b) + .05) >= 4.5,
                            "theme text contrast below 4.5:1");
                }
        }
        std::cout << "PASS theme persistence values and text contrast in both palettes\n";
        std::deque<EngineJob> jobs;
        pushEngineJob(jobs, {EngineJob::Seek, "seek", "10", "absolute+exact"});
        pushEngineJob(jobs, {EngineJob::Seek, "seek", "20", "absolute+exact"});
        require(jobs.size() == 1 && jobs.front().b == "20", "adjacent seek coalescing");
        pushEngineJob(jobs, {EngineJob::Command, "loadfile", "next.mkv"});
        pushEngineJob(jobs, {EngineJob::Seek, "seek", "30", "absolute+exact"});
        require(jobs.size() == 3 && jobs[0].b == "20" && jobs[1].a == "loadfile",
                "seek crossed a media-load boundary");
        jobs.clear();
        pushEngineJob(jobs, {EngineJob::Property, "volume", "10"});
        pushEngineJob(jobs, {EngineJob::Property, "volume", "20"});
        require(jobs.size() == 1 && jobs.front().b == "20", "adjacent volume coalescing");
        pushEngineJob(jobs, {EngineJob::Command, "stop"});
        pushEngineJob(jobs, {EngineJob::Property, "volume", "30"});
        require(jobs.size() == 3 && jobs[0].b == "20" && jobs[1].a == "stop",
                "property crossed a stop boundary");
        jobs.clear();
        pushEngineJob(jobs, {EngineJob::Seek, "seek", "10"});
        pushEngineJob(jobs, {EngineJob::Property, "pause", "yes"});
        pushEngineJob(jobs, {EngineJob::Screenshot, ".", "png"});
        pushEngineJob(jobs, {EngineJob::Seek, "seek", "20"});
        require(jobs.size() == 4 && jobs[0].b == "10" && jobs[1].a == "pause",
                "seek crossed pause/screenshot boundaries");
        std::cout << "PASS queued commands preserve load, stop, pause and screenshot ordering\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}

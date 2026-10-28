#define NOMINMAX
#define UNICODE
#include "../src/engine.hpp"
#include "../src/playback.hpp"
#include <iostream>
#include <stdexcept>
#include "../src/components.hpp"

void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F>
PlaybackState awaitState(Engine &engine, F predicate, const char *message, int timeout = 8000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    PlaybackState state;
    do {
        state = engine.snapshot();
        if (predicate(state))
            return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < end);
    std::wcerr << L"Last engine error: " << state.error << L"\n";
    throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 3, "Usage: integration.exe DLL fixture.mkv");
        comp::Release release;
        release.assets.push_back({"mpv-dev-x86_64-v3-test.7z", "https://example.invalid/v3", "", 1});
        require(!comp::pickEngine(release, false), "AVX2 build accepted on a baseline CPU");
        require(comp::pickEngine(release, true), "AVX2 build unavailable on a compatible CPU");
        release.assets.push_back({"mpv-dev-x86_64-test.7z", "https://example.invalid/base", "", 1});
        require(comp::pickEngine(release, false)->name == "mpv-dev-x86_64-test.7z",
                "baseline engine selection");
        std::cout << "PASS libmpv package selection and CPU compatibility\n";
        require(parseTime(L"12:30") == 750, "minutes:seconds");
        require(parseTime(L"01:12:30") == 4350, "hours:minutes:seconds");
        require(parseTime(L"90") == 90, "seconds");
        require(parseTime(L"1:02,5") == 62.5, "fractional seconds");
        for (const auto &bad : {L"", L"-1", L"1:60", L"1::2", L"1:", L"1:2:3:4", L"nan", L"1e9", L"1.2:3",
                                L"12x", L"10000000000"})
            require(!parseTime(bad), "invalid time accepted");
        require(clockText(4350) == L"01:12:30", "clock with hours");
        require(clockText(-10) == L"00:00", "negative clock");
        require(positionAt(200, 0, 100, 120) == 120, "slider end clamp");
        require(positionAt(-10, 0, 100, 120) == 0, "slider start clamp");
        require(positionAt(25, 0, 100, 120) == 30, "slider mapping");
        std::cout << "PASS time parsing, validation and timeline mapping\n";
        Engine engine;
        engine.start(argv[1], nullptr, L".", "png", true);
        awaitState(engine, [](auto &s) { return s.ready; }, "engine initialization");
        const auto path = utf8(argv[2]);
        engine.command("loadfile", path);
        auto state = awaitState(
            engine, [](auto &s) { return s.loaded && s.tracks.size() == 6 && s.duration > 119; },
            "load multitrack fixture");
        require(state.seekable, "local file is not seekable");
        int audio = 0, video = 0, sub = 0;
        for (auto &t : state.tracks) {
            audio += t.type == "audio";
            video += t.type == "video";
            sub += t.type == "sub";
        }
        require(audio == 2 && video == 2 && sub == 2, "track enumeration");
        std::cout << "PASS enumerate two audio/video/subtitle tracks\n";
        engine.property("pause", "yes");
        awaitState(engine, [](auto &s) { return s.paused; }, "pause");
        for (double target : {35., 90., 1., 60.}) {
            engine.seek(target);
            awaitState(
                engine, [&](auto &s) { return s.paused && std::abs(s.time - target) < .15; },
                "exact paused seek");
        }
        std::cout << "PASS exact seeks forward and backward while paused\n";
        awaitState(engine, [](auto &s) { return s.bufferedSeconds > 5; }, "local read-ahead buffer");
        for (int pauseMs : {1000, 5000, 30000}) {
            engine.property("pause", "yes");
            auto paused = awaitState(engine, [](auto &s) { return s.paused; }, "pause for resume test");
            std::this_thread::sleep_for(std::chrono::milliseconds(pauseMs));
            require(std::abs(engine.snapshot().time - paused.time) < .15,
                    "playback advanced during pause");
            auto start = std::chrono::steady_clock::now();
            engine.property("pause", "no");
            awaitState(engine, [&](auto &s) { return !s.paused && s.time > paused.time + .2; },
                       "resume did not advance playback", 3000);
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start).count();
            std::cout << "PASS resume after " << pauseMs << " ms pause: " << latency << " ms\n";
        }
        engine.seek(60, 1);
        awaitState(engine, [](auto &s) { return s.paused && std::abs(s.time - 60) < .15; },
                   "restore paused position after resume test");
        auto captureFailure = engine.snapshot().captureErrorSerial;
        engine.capture(wide(path), "png"); // A media file cannot also be a directory.
        awaitState(
            engine, [&](auto &s) { return s.captureErrorSerial > captureFailure && !s.capturing; },
            "invalid screenshot directory reported");
        require(engine.snapshot().lastScreenshot.empty(), "failed capture reported as saved");
        require(engine.snapshot().captureError.find(L"cartella") != std::wstring::npos,
                "directory failure not identified");
        std::cout << "PASS screenshot failure reports an error without a false success\n";
        for (auto type : {"audio", "video", "sub"}) {
            engine.property(std::string(type) == "audio"   ? "aid"
                            : std::string(type) == "video" ? "vid"
                                                           : "sid",
                            "2");
            awaitState(
                engine,
                [&](auto &s) {
                    for (auto &t : s.tracks)
                        if (t.type == type && t.id == 2 && t.selected)
                            return true;
                    return false;
                },
                "select second track");
        }
        engine.property("sid", "no");
        awaitState(
            engine,
            [](auto &s) {
                for (auto &t : s.tracks)
                    if (t.type == "sub" && t.selected)
                        return false;
                return true;
            },
            "disable subtitles");
        std::cout << "PASS switch audio, video, subtitles and disable subtitles\n";
        engine.property("speed", "1.5");
        engine.property("volume", "43");
        engine.property("mute", "yes");
        awaitState(
            engine, [](auto &s) { return s.speed == 1.5 && s.volume == 43 && s.muted; }, "speed volume mute");
        engine.property("pause", "no");
        awaitState(engine, [](auto &s) { return !s.paused && s.time > 60.5; }, "resume playback");
        std::cout << "PASS speed, volume, mute, resume\n";
        for (int i = 0; i < 6; ++i) {
            engine.command("stop");
            awaitState(engine, [](auto &s) { return !s.loaded && !s.loading; }, "stop");
            engine.command("loadfile", path);
            awaitState(engine, [](auto &s) { return s.loaded && s.tracks.size() == 6; }, "reload");
        }
        std::cout << "PASS repeated stop and reload\n";
        engine.property("pause", "yes");
        awaitState(engine, [](auto &s) { return s.paused; }, "pause before stress");
        std::thread producer([&] {
            for (int i = 0; i < 150; ++i)
                engine.seek(i % 100);
            engine.seek(42);
        });
        for (int i = 0; i < 1000; ++i) {
            auto begin = std::chrono::steady_clock::now();
            engine.snapshot();
            require(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(100),
                    "snapshot blocked UI");
        }
        producer.join();
        awaitState(
            engine, [](auto &s) { return std::abs(s.time - 42) < .15; }, "coalesced seek final position");
        std::cout << "PASS concurrent snapshots and queued seek stress\n";
        engine.seek(119.9, 0);
        awaitState(engine, [](auto &s) { return s.eof; }, "end of file");
        engine.seek(30, 1);
        awaitState(
            engine, [](auto &s) { return s.paused && !s.eof && std::abs(s.time - 30) < .15; },
            "paused seek after EOF");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        state = engine.snapshot();
        require(state.paused && std::abs(state.time - 30) < .15, "seek unexpectedly resumed playback");
        std::cout << "PASS seek from end of file preserves pause\n";
        auto serial = engine.snapshot().errorSerial;
        engine.command("loadfile", path + ".missing");
        awaitState(
            engine, [&](auto &s) { return s.errorSerial > serial && !s.loaded && !s.loading; },
            "invalid file handling");
        engine.command("loadfile", path);
        awaitState(engine, [](auto &s) { return s.loaded; }, "recover after failed load");
        engine.stop();
        std::cout << "PASS error recovery and clean worker shutdown\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}

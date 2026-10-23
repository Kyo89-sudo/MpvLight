#pragma once
#include "player.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <filesystem>
#include <unordered_map>

struct PlaybackState {
    bool ready{}, loaded{}, loading{}, paused{true}, seekable{}, eof{}, muted{}, capturing{}, videoReady{};
    double time{}, duration{}, volume{75}, speed{1}, videoW{}, videoH{};
    unsigned trackRevision{};
    std::wstring title, version, decoder, error, notice, lastScreenshot, captureError;
    uint64_t errorSerial{}, noticeSerial{}, captureErrorSerial{};
    std::vector<Track> tracks;
};

// This is the only thread that calls libmpv. No decoder/property wait runs on
// the UI thread.
class Engine {
    struct Job {
        enum Kind { Command, Property, Seek, Screenshot } kind;
        std::string a, b, c;
        uint64_t reply{1};
        int pauseAfter{-1};
    };
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> wakeup_{};
    std::deque<Job> jobs_;
    bool stopping_{};
    PlaybackState shared_;
    void enqueue(Job job) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
            return;
        // Dragging a timeline must never create an old-seek backlog.
        if (job.kind == Job::Seek)
            for (auto it = jobs_.begin(); it != jobs_.end();) {
                if (it->kind == Job::Seek)
                    it = jobs_.erase(it);
                else
                    ++it;
            }
        if (job.kind == Job::Property && (job.a == "volume" || job.a == "speed")) {
            for (auto it = jobs_.begin(); it != jobs_.end();) {
                if (it->kind == Job::Property && it->a == job.a)
                    it = jobs_.erase(it);
                else
                    ++it;
            }
        }
        jobs_.push_back(std::move(job));
        cv_.notify_one();
    }

  public:
    ~Engine() {
        stop();
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            cv_.notify_one();
        }
        if (thread_.joinable())
            thread_.join();
    }
    PlaybackState snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return shared_;
    }
    void command(std::string a, std::string b = {}, std::string c = {}, uint64_t reply = 1) {
        enqueue({Job::Command, std::move(a), std::move(b), std::move(c), reply});
    }
    void property(std::string a, std::string b) {
        enqueue({Job::Property, std::move(a), std::move(b), {}});
    }
    // exact=false seeks to the nearest keyframe: used while dragging the timeline.
    void seek(double time, int paused = -1, bool exact = true) {
        enqueue({Job::Seek, "seek", std::to_string(time), exact ? "absolute+exact" : "absolute+keyframes", 1,
                 paused});
    }
    void capture(const std::wstring &directory, const std::string &format, bool subtitles = true) {
        enqueue({Job::Screenshot, utf8(directory), format == "jpg" ? "jpg" : "png",
                 subtitles ? "subtitles" : "video"});
    }
    void start(const std::wstring &dll, HWND video, const std::wstring &shots, const std::string &format,
               bool headless = false) {
        thread_ = std::thread([this, dll, video, shots, format, headless] {
            Player p;
            PlaybackState state;
            auto publish = [&] {
                std::lock_guard<std::mutex> lock(mutex_);
                shared_ = state;
            };
            auto error = [&](std::wstring message) {
                state.error = std::move(message);
                ++state.errorSerial;
            };
            auto captureError = [&](std::wstring message) {
                state.captureError = std::move(message);
                ++state.captureErrorSerial;
                state.capturing = false;
            };
            std::unordered_map<uint64_t, std::wstring> captures;
            uint64_t captureId = 1000;
            if (!p.load(dll)) {
                error(p.failure);
                publish();
                return;
            }
            auto id = std::to_string(reinterpret_cast<uintptr_t>(video));
            p.option(p.h, "config", "no");
            p.option(p.h, "wid", id.c_str());
            if (headless) {
                p.option(p.h, "vo", "null");
                p.option(p.h, "ao", "null");
            }
            p.option(p.h, "idle", "yes");
            p.option(p.h, "keep-open", "yes");
            p.option(p.h, "osc", "no");
            p.option(p.h, "input-default-bindings", "no");
            p.option(p.h, "input-vo-keyboard", "no");
            // Mouse events go through mpv's video window to MpvLight's own window.
            p.option(p.h, "input-cursor-passthrough", "yes");
            p.option(p.h, "cursor-autohide-fs-only", "yes");
            p.option(p.h, "osd-level", "0");
            p.option(p.h, "hwdec", "auto-safe");
            p.option(p.h, "volume", "75");
            p.option(p.h, "screenshot-format", format.c_str());
            auto shotDir = utf8(shots);
            p.option(p.h, "screenshot-directory", shotDir.c_str());
            int init = p.initialize(p.h);
            if (init < 0) {
                error(L"Avvio del motore non riuscito: " + wide(p.errorString(init)));
                publish();
                return;
            }
            state.ready = true;
            state.version = p.text("mpv-version");
            p.observe(p.h, 1, "track-list", MPV_FORMAT_NONE);
            p.setWakeup(
                p.h,
                [](void *context) {
                    auto self = static_cast<Engine *>(context);
                    self->wakeup_.store(true);
                    self->cv_.notify_one();
                },
                this);
            bool tracksDirty = true;
            auto next = std::chrono::steady_clock::now();
            for (;;) {
                std::deque<Job> batch;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (stopping_)
                        break;
                    batch.swap(jobs_);
                }
                for (const auto &job : batch) {
                    int result = 0;
                    if (job.kind == Job::Screenshot) {
                        if (state.capturing)
                            continue;
                        if (!state.loaded || !state.videoReady) {
                            captureError(L"Nessun fotogramma video disponibile. Avvia un video e riprova.");
                            continue;
                        }
                        std::filesystem::path directory(wide(job.a));
                        std::error_code ec;
                        directory = std::filesystem::absolute(directory, ec);
                        if (!ec)
                            std::filesystem::create_directories(directory, ec);
                        if (ec) {
                            captureError(L"Impossibile creare la cartella screenshot:\n" +
                                         directory.wstring() +
                                         L"\n\nScegli una cartella scrivibile dal menu Screenshot.");
                            continue;
                        }
                        SYSTEMTIME now;
                        GetLocalTime(&now);
                        wchar_t filename[160];
                        ++captureId;
                        swprintf_s(filename, L"MpvLight_%04u-%02u-%02u_%02u-%02u-%02u-%03u_%lu_%llu.%s",
                                   now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                                   now.wMilliseconds, GetCurrentProcessId(), captureId, wide(job.b).c_str());
                        auto output = (directory / filename).wstring();
                        // Check permissions before asking mpv to encode; the probe is removed on close.
                        HANDLE probe =
                            CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
                        if (probe == INVALID_HANDLE_VALUE) {
                            captureError(L"La cartella screenshot non è scrivibile:\n" + directory.wstring() +
                                         L"\n\nScegli un'altra cartella dal menu Screenshot.");
                            continue;
                        }
                        CloseHandle(probe);
                        state.capturing = true;
                        auto name = utf8(output);
                        result = p.cmd("screenshot-to-file", name.c_str(), job.c.c_str(), captureId);
                        if (result < 0) {
                            captureError(L"Cattura non riuscita: " + wide(p.errorString(result)));
                        } else
                            captures.emplace(captureId, output);
                        continue;
                    } else if (job.kind == Job::Seek) {
                        bool paused = job.pauseAfter < 0 ? p.flag("pause") : job.pauseAfter != 0;
                        const char *args[] = {"seek", job.b.c_str(), job.c.c_str(), nullptr};
                        result = p.command(p.h, args);
                        if (result >= 0)
                            p.property("pause", paused ? "yes" : "no");
                    } else if (job.kind == Job::Property)
                        result = p.property(job.a.c_str(), job.b);
                    else
                        result = p.cmd(job.a.c_str(), job.b.empty() ? nullptr : job.b.c_str(),
                                       job.c.empty() ? nullptr : job.c.c_str(), job.reply);
                    if (result < 0)
                        error(L"Operazione non riuscita: " + wide(p.errorString(result)));
                }
                wakeup_.store(false);
                for (int i = 0; i < 256; ++i) {
                    auto ev = p.wait(p.h, 0);
                    if (ev->event_id == MPV_EVENT_NONE)
                        break;
                    switch (ev->event_id) {
                    case MPV_EVENT_START_FILE:
                        state.loading = true;
                        state.loaded = false;
                        state.time = state.duration = 0;
                        state.tracks.clear();
                        ++state.trackRevision;
                        break;
                    case MPV_EVENT_FILE_LOADED:
                        state.loaded = true;
                        state.loading = false;
                        state.title = p.text("media-title");
                        p.property("pause", "no");
                        tracksDirty = true;
                        break;
                    case MPV_EVENT_END_FILE: {
                        state.loading = false;
                        state.loaded = false;
                        state.time = state.duration = 0;
                        state.tracks.clear();
                        ++state.trackRevision;
                        auto end = static_cast<mpv_event_end_file *>(ev->data);
                        if (end && end->reason == MPV_END_FILE_REASON_ERROR)
                            error(L"Impossibile riprodurre il file: " + wide(p.errorString(end->error)));
                        break;
                    }
                    case MPV_EVENT_PROPERTY_CHANGE:
                        tracksDirty = true;
                        break;
                    case MPV_EVENT_COMMAND_REPLY:
                        if (auto pending = captures.find(ev->reply_userdata); pending != captures.end()) {
                            auto filename = pending->second;
                            std::error_code ec;
                            auto size = std::filesystem::file_size(filename, ec);
                            if (ev->error < 0 || ec || size == 0) {
                                captureError(L"Screenshot non salvato:\n" + filename + L"\n\n" +
                                             (ev->error < 0 ? wide(p.errorString(ev->error))
                                                            : L"File immagine assente o vuoto."));
                            } else {
                                state.lastScreenshot = filename;
                                state.notice = L"Salvato: " + filename;
                                ++state.noticeSerial;
                                state.capturing = false;
                            }
                            captures.erase(pending);
                            break;
                        }
                        if (ev->error < 0)
                            error(L"Operazione non riuscita: " + wide(p.errorString(ev->error)));
                        else if (ev->reply_userdata == 2) {
                            state.notice = L"Screenshot salvato";
                            ++state.noticeSerial;
                        }
                        break;
                    default:
                        break;
                    }
                }
                if (tracksDirty) {
                    state.tracks = p.tracks();
                    ++state.trackRevision;
                    tracksDirty = false;
                }
                auto now = std::chrono::steady_clock::now();
                if (now >= next || !batch.empty()) {
                    state.time = p.number("time-pos");
                    state.duration = p.number("duration");
                    state.paused = p.flag("pause", true);
                    state.eof = p.flag("eof-reached");
                    state.seekable = state.loaded && p.flag("seekable");
                    state.volume = p.number("volume", 75);
                    state.muted = p.flag("mute");
                    state.speed = p.number("speed", 1);
                    state.decoder = state.loaded ? p.text("hwdec-current") : L"";
                    state.videoReady = state.loaded && p.number("video-out-params/w") > 0;
                    // Display size (after aspect ratio and rotation), used to fit the window to the video.
                    state.videoW = state.videoReady ? p.number("video-out-params/dw") : 0;
                    state.videoH = state.videoReady ? p.number("video-out-params/dh") : 0;
                    publish();
                    next = now + std::chrono::milliseconds(100);
                }
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait_until(lock, next, [&] { return stopping_ || !jobs_.empty() || wakeup_.load(); });
                    if (stopping_)
                        break;
                }
            }
            p.setWakeup(p.h, nullptr, nullptr);
        });
    }
};

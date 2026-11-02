#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

struct EngineJob {
    enum Kind { Command, Property, Seek, Screenshot } kind;
    std::string a, b, c;
    uint64_t reply{1};
    int pauseAfter{-1};
};

inline void pushEngineJob(std::deque<EngineJob> &jobs, EngineJob job) {
    // Only merge adjacent updates. Crossing loadfile, stop, pause or any other
    // intervening operation can move a seek/property to the wrong media file.
    if (!jobs.empty() && jobs.back().kind == job.kind &&
        (job.kind == EngineJob::Seek ||
         (job.kind == EngineJob::Property && jobs.back().a == job.a &&
          (job.a == "volume" || job.a == "speed")))) {
        jobs.back() = std::move(job);
        return;
    }
    jobs.push_back(std::move(job));
}

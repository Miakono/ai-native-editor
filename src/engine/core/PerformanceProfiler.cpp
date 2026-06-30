#include "engine/core/PerformanceProfiler.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <set>
#include <sstream>
#include <system_error>

namespace aine {

namespace {

using json = nlohmann::json;

constexpr double kSlowFrameMs = 33.3;
constexpr double kSlowZoneMs = 8.0;

double Percentile95(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index =
        std::min(values.size() - 1, static_cast<std::size_t>(std::ceil(static_cast<double>(values.size()) * 0.95) - 1.0));
    return values[index];
}

double CounterValue(const std::vector<PerformanceCounterSummary>& counters, const std::string& name) {
    auto it = std::find_if(counters.begin(), counters.end(), [&name](const PerformanceCounterSummary& counter) {
        return counter.name == name;
    });
    return it == counters.end() ? 0.0 : it->value;
}

const PerformanceZoneSummary* FindZone(const std::vector<PerformanceZoneSummary>& zones, const std::string& name) {
    auto it = std::find_if(zones.begin(), zones.end(), [&name](const PerformanceZoneSummary& zone) {
        return zone.name == name;
    });
    return it == zones.end() ? nullptr : &(*it);
}

json ZoneToJson(const PerformanceZoneSummary& zone) {
    return {
        {"domain", PerformanceDomainLabel(zone.domain)},
        {"name", zone.name},
        {"samples", zone.sampleCount},
        {"lastMs", zone.lastMs},
        {"minMs", zone.minMs},
        {"avgMs", zone.avgMs},
        {"maxMs", zone.maxMs},
        {"p95Ms", zone.p95Ms},
        {"totalMs", zone.totalMs},
    };
}

json CounterToJson(const PerformanceCounterSummary& counter) {
    return {
        {"domain", PerformanceDomainLabel(counter.domain)},
        {"name", counter.name},
        {"value", counter.value},
    };
}

json SpikeToJson(const PerformanceSpike& spike) {
    return {
        {"frame", spike.frameIndex},
        {"domain", PerformanceDomainLabel(spike.domain)},
        {"zone", spike.zoneName},
        {"ms", spike.ms},
    };
}

} // namespace

const char* PerformanceDomainLabel(PerformanceDomain domain) {
    switch (domain) {
    case PerformanceDomain::Runtime:
        return "Runtime";
    case PerformanceDomain::Editor:
    default:
        return "Editor";
    }
}

void PerformanceProfiler::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        paused_ = false;
        frameActive_ = false;
    }
}

void PerformanceProfiler::SetPaused(bool paused) {
    paused_ = paused;
}

void PerformanceProfiler::SetCapacity(std::size_t capacity) {
    capacity_ = std::clamp<std::size_t>(capacity, 30, 3600);
    while (frames_.size() > capacity_) {
        frames_.erase(frames_.begin());
    }
}

void PerformanceProfiler::Clear() {
    frames_.clear();
    currentFrame_ = FrameRecord{};
    frameActive_ = false;
    nextFrameIndex_ = 1;
}

void PerformanceProfiler::BeginFrame() {
    if (!IsCapturing()) {
        frameActive_ = false;
        return;
    }
    currentFrame_ = FrameRecord{};
    currentFrame_.frameIndex = nextFrameIndex_++;
    frameActive_ = true;
}

void PerformanceProfiler::EndFrame() {
    if (!frameActive_) {
        return;
    }
    frames_.push_back(currentFrame_);
    while (frames_.size() > capacity_) {
        frames_.erase(frames_.begin());
    }
    currentFrame_ = FrameRecord{};
    frameActive_ = false;
}

void PerformanceProfiler::RecordZoneDuration(PerformanceDomain domain, const std::string& name, double durationMs) {
    if (!IsCapturing() || !frameActive_ || name.empty() || durationMs < 0.0 || !std::isfinite(durationMs)) {
        return;
    }
    MutableZonesForDomain(currentFrame_, domain)[name] += durationMs;
}

void PerformanceProfiler::RecordCounter(PerformanceDomain domain, const std::string& name, double value) {
    if (!IsCapturing() || !frameActive_ || name.empty() || !std::isfinite(value)) {
        return;
    }
    MutableCountersForDomain(currentFrame_, domain)[name] = value;
}

PerformanceProfilerSnapshot PerformanceProfiler::Snapshot() const {
    PerformanceProfilerSnapshot snapshot;
    snapshot.frameCount = static_cast<int>(frames_.size());
    if (!frames_.empty()) {
        const FrameRecord& last = frames_.back();
        snapshot.lastFrameIndex = last.frameIndex;
        auto editorIt = last.editorZonesMs.find("Editor.Frame");
        auto runtimeIt = last.runtimeZonesMs.find("Runtime.Frame");
        snapshot.lastEditorFrameMs = editorIt == last.editorZonesMs.end() ? 0.0 : editorIt->second;
        snapshot.lastRuntimeFrameMs = runtimeIt == last.runtimeZonesMs.end() ? 0.0 : runtimeIt->second;
    }
    snapshot.zones = SummarizeZones();
    snapshot.counters = SummarizeCounters();
    snapshot.spikes = SummarizeSpikes();
    snapshot.guidance = BuildOptimizationGuidance(snapshot.zones, snapshot.counters);
    return snapshot;
}

std::vector<std::string> PerformanceProfiler::BuildOptimizationGuidance(
    const std::vector<PerformanceZoneSummary>& zones,
    const std::vector<PerformanceCounterSummary>& counters) const {
    std::vector<std::string> guidance;

    const PerformanceZoneSummary* editorFrame = FindZone(zones, "Editor.Frame");
    const PerformanceZoneSummary* runtimeFrame = FindZone(zones, "Runtime.Frame");
    if (editorFrame != nullptr && runtimeFrame != nullptr && editorFrame->p95Ms > runtimeFrame->p95Ms * 1.75 &&
        editorFrame->p95Ms > 16.6) {
        guidance.push_back("Editor frame cost is higher than runtime cost; focus on editor panels, dock UI, logs, and view redraws before changing gameplay.");
    }

    for (const PerformanceZoneSummary& zone : zones) {
        if (zone.sampleCount < 2 || zone.p95Ms < kSlowZoneMs) {
            continue;
        }
        if (zone.name.find("ProjectTree") != std::string::npos) {
            guidance.push_back("Project tree/file browsing is a hot zone; cache directory entries, refresh incrementally, and keep generated folders hidden from the main tree.");
        } else if (zone.name.find("Console") != std::string::npos || zone.name.find("AgentPanel") != std::string::npos) {
            guidance.push_back("Logs or agent history are taking measurable frame time; bound previews, virtualize long lists, and avoid full JSONL/log rereads during every frame.");
        } else if (zone.name.find("Physics") != std::string::npos) {
            std::ostringstream line;
            line << "Physics is a hot zone with " << CounterValue(counters, "Runtime.PhysicsBodies") << " bodies and "
                 << CounterValue(counters, "Runtime.PhysicsColliders")
                 << " colliders; inspect collider counts, fixed-step settings, sleeping/deactivation, and broad-phase pruning.";
            guidance.push_back(line.str());
        } else if (zone.name.find("Scripting") != std::string::npos) {
            std::ostringstream line;
            line << "Scripting is a hot zone with " << CounterValue(counters, "Runtime.ActiveScripts")
                 << " active scripts; reduce per-frame script invocation, batch host calls, and move stable data out of OnUpdate paths.";
            guidance.push_back(line.str());
        } else if (zone.name.find("GameView") != std::string::npos || zone.name.find("SceneView") != std::string::npos ||
                   zone.name.find("RenderPrep") != std::string::npos) {
            guidance.push_back("Viewport rendering/prep is a hot zone; check render queue construction, terrain/cave cache invalidation, gizmo/debug overlays, and framebuffer resize churn.");
        }
    }

    if (guidance.empty()) {
        guidance.push_back("No dominant measured hotspot yet; capture longer while reproducing the slowdown, then sort by p95 and max frame cost.");
    }
    return guidance;
}

bool PerformanceProfiler::ExportReport(const std::filesystem::path& outputPath, std::string* error) const {
    if (outputPath.empty()) {
        if (error != nullptr) {
            *error = "Profiler export path is empty.";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        if (error != nullptr) {
            *error = "Could not create profiler export folder: " + ec.message();
        }
        return false;
    }

    const PerformanceProfilerSnapshot snapshot = Snapshot();
    json document;
    document["frameCount"] = snapshot.frameCount;
    document["lastFrameIndex"] = snapshot.lastFrameIndex;
    document["lastEditorFrameMs"] = snapshot.lastEditorFrameMs;
    document["lastRuntimeFrameMs"] = snapshot.lastRuntimeFrameMs;
    document["zones"] = json::array();
    document["counters"] = json::array();
    document["spikes"] = json::array();
    document["guidance"] = snapshot.guidance;
    for (const PerformanceZoneSummary& zone : snapshot.zones) {
        document["zones"].push_back(ZoneToJson(zone));
    }
    for (const PerformanceCounterSummary& counter : snapshot.counters) {
        document["counters"].push_back(CounterToJson(counter));
    }
    for (const PerformanceSpike& spike : snapshot.spikes) {
        document["spikes"].push_back(SpikeToJson(spike));
    }

    std::ofstream stream(outputPath);
    if (!stream) {
        if (error != nullptr) {
            *error = "Could not open profiler export path: " + outputPath.string();
        }
        return false;
    }
    stream << document.dump(2) << '\n';
    return true;
}

std::string PerformanceProfiler::ZoneKey(PerformanceDomain domain, const std::string& name) {
    return std::string(PerformanceDomainLabel(domain)) + "." + name;
}

const std::map<std::string, double>& PerformanceProfiler::ZonesForDomain(const FrameRecord& frame,
                                                                          PerformanceDomain domain) {
    return domain == PerformanceDomain::Runtime ? frame.runtimeZonesMs : frame.editorZonesMs;
}

const std::map<std::string, double>& PerformanceProfiler::CountersForDomain(const FrameRecord& frame,
                                                                             PerformanceDomain domain) {
    return domain == PerformanceDomain::Runtime ? frame.runtimeCounters : frame.editorCounters;
}

std::map<std::string, double>& PerformanceProfiler::MutableZonesForDomain(FrameRecord& frame,
                                                                           PerformanceDomain domain) {
    return domain == PerformanceDomain::Runtime ? frame.runtimeZonesMs : frame.editorZonesMs;
}

std::map<std::string, double>& PerformanceProfiler::MutableCountersForDomain(FrameRecord& frame,
                                                                              PerformanceDomain domain) {
    return domain == PerformanceDomain::Runtime ? frame.runtimeCounters : frame.editorCounters;
}

std::vector<PerformanceZoneSummary> PerformanceProfiler::SummarizeZones() const {
    std::vector<PerformanceZoneSummary> summaries;
    std::set<std::string> names;
    for (const FrameRecord& frame : frames_) {
        for (PerformanceDomain domain : {PerformanceDomain::Editor, PerformanceDomain::Runtime}) {
            for (const auto& [name, value] : ZonesForDomain(frame, domain)) {
                names.insert(ZoneKey(domain, name));
            }
        }
    }

    for (const std::string& key : names) {
        const bool runtime = key.rfind("Runtime.", 0) == 0;
        const PerformanceDomain domain = runtime ? PerformanceDomain::Runtime : PerformanceDomain::Editor;
        const std::string name = key.substr(runtime ? 8 : 7);
        std::vector<double> samples;
        samples.reserve(frames_.size());
        double last = 0.0;
        for (const FrameRecord& frame : frames_) {
            const auto& zones = ZonesForDomain(frame, domain);
            auto it = zones.find(name);
            if (it != zones.end()) {
                samples.push_back(it->second);
                last = it->second;
            }
        }
        if (samples.empty()) {
            continue;
        }
        PerformanceZoneSummary summary;
        summary.domain = domain;
        summary.name = name;
        summary.sampleCount = static_cast<int>(samples.size());
        summary.lastMs = last;
        summary.minMs = *std::min_element(samples.begin(), samples.end());
        summary.maxMs = *std::max_element(samples.begin(), samples.end());
        summary.totalMs = std::accumulate(samples.begin(), samples.end(), 0.0);
        summary.avgMs = summary.totalMs / static_cast<double>(samples.size());
        summary.p95Ms = Percentile95(samples);
        summaries.push_back(summary);
    }

    std::sort(summaries.begin(), summaries.end(), [](const PerformanceZoneSummary& left,
                                                     const PerformanceZoneSummary& right) {
        if (left.p95Ms == right.p95Ms) {
            return left.maxMs > right.maxMs;
        }
        return left.p95Ms > right.p95Ms;
    });
    return summaries;
}

std::vector<PerformanceCounterSummary> PerformanceProfiler::SummarizeCounters() const {
    std::vector<PerformanceCounterSummary> summaries;
    if (frames_.empty()) {
        return summaries;
    }
    const FrameRecord& last = frames_.back();
    for (PerformanceDomain domain : {PerformanceDomain::Editor, PerformanceDomain::Runtime}) {
        for (const auto& [name, value] : CountersForDomain(last, domain)) {
            summaries.push_back({domain, name, value});
        }
    }
    std::sort(summaries.begin(), summaries.end(), [](const PerformanceCounterSummary& left,
                                                     const PerformanceCounterSummary& right) {
        if (left.domain != right.domain) {
            return left.domain == PerformanceDomain::Editor;
        }
        return left.name < right.name;
    });
    return summaries;
}

std::vector<PerformanceSpike> PerformanceProfiler::SummarizeSpikes() const {
    std::vector<PerformanceSpike> spikes;
    for (const FrameRecord& frame : frames_) {
        for (PerformanceDomain domain : {PerformanceDomain::Editor, PerformanceDomain::Runtime}) {
            for (const auto& [name, value] : ZonesForDomain(frame, domain)) {
                const bool frameTotal = name == "Editor.Frame" || name == "Runtime.Frame";
                if ((frameTotal && value >= kSlowFrameMs) || (!frameTotal && value >= kSlowZoneMs)) {
                    spikes.push_back({frame.frameIndex, domain, name, value});
                }
            }
        }
    }
    std::sort(spikes.begin(), spikes.end(), [](const PerformanceSpike& left, const PerformanceSpike& right) {
        return left.ms > right.ms;
    });
    if (spikes.size() > 16) {
        spikes.resize(16);
    }
    return spikes;
}

PerformanceScope::PerformanceScope(PerformanceProfiler* profiler, PerformanceDomain domain, const char* name)
    : PerformanceScope(profiler, domain, std::string(name == nullptr ? "" : name)) {}

PerformanceScope::PerformanceScope(PerformanceProfiler* profiler, PerformanceDomain domain, const std::string& name)
    : profiler_(profiler), domain_(domain), name_(name) {
    active_ = profiler_ != nullptr && profiler_->IsCapturing() && !name_.empty();
    if (active_) {
        started_ = std::chrono::steady_clock::now();
    }
}

PerformanceScope::~PerformanceScope() {
    if (!active_ || profiler_ == nullptr) {
        return;
    }
    const auto finished = std::chrono::steady_clock::now();
    const double durationMs = std::chrono::duration<double, std::milli>(finished - started_).count();
    profiler_->RecordZoneDuration(domain_, name_, durationMs);
}

} // namespace aine

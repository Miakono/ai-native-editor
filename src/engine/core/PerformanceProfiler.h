#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace aine {

enum class PerformanceDomain {
    Editor,
    Runtime
};

const char* PerformanceDomainLabel(PerformanceDomain domain);

struct PerformanceZoneSummary {
    PerformanceDomain domain = PerformanceDomain::Editor;
    std::string name;
    int sampleCount = 0;
    double lastMs = 0.0;
    double minMs = 0.0;
    double avgMs = 0.0;
    double maxMs = 0.0;
    double p95Ms = 0.0;
    double totalMs = 0.0;
};

struct PerformanceCounterSummary {
    PerformanceDomain domain = PerformanceDomain::Editor;
    std::string name;
    double value = 0.0;
};

struct PerformanceSpike {
    int frameIndex = 0;
    PerformanceDomain domain = PerformanceDomain::Editor;
    std::string zoneName;
    double ms = 0.0;
};

struct PerformanceProfilerSnapshot {
    int frameCount = 0;
    int lastFrameIndex = 0;
    double lastEditorFrameMs = 0.0;
    double lastRuntimeFrameMs = 0.0;
    std::vector<PerformanceZoneSummary> zones;
    std::vector<PerformanceCounterSummary> counters;
    std::vector<PerformanceSpike> spikes;
    std::vector<std::string> guidance;
};

class PerformanceProfiler {
public:
    bool Enabled() const { return enabled_; }
    bool Paused() const { return paused_; }
    bool IsCapturing() const { return enabled_ && !paused_; }
    std::size_t Capacity() const { return capacity_; }

    void SetEnabled(bool enabled);
    void SetPaused(bool paused);
    void SetCapacity(std::size_t capacity);
    void Clear();

    void BeginFrame();
    void EndFrame();
    void RecordZoneDuration(PerformanceDomain domain, const std::string& name, double durationMs);
    void RecordCounter(PerformanceDomain domain, const std::string& name, double value);

    PerformanceProfilerSnapshot Snapshot() const;
    std::vector<std::string> BuildOptimizationGuidance(const std::vector<PerformanceZoneSummary>& zones,
                                                       const std::vector<PerformanceCounterSummary>& counters) const;
    bool ExportReport(const std::filesystem::path& outputPath, std::string* error = nullptr) const;

private:
    struct FrameRecord {
        int frameIndex = 0;
        std::map<std::string, double> editorZonesMs;
        std::map<std::string, double> runtimeZonesMs;
        std::map<std::string, double> editorCounters;
        std::map<std::string, double> runtimeCounters;
    };

    static std::string ZoneKey(PerformanceDomain domain, const std::string& name);
    static const std::map<std::string, double>& ZonesForDomain(const FrameRecord& frame, PerformanceDomain domain);
    static const std::map<std::string, double>& CountersForDomain(const FrameRecord& frame, PerformanceDomain domain);
    static std::map<std::string, double>& MutableZonesForDomain(FrameRecord& frame, PerformanceDomain domain);
    static std::map<std::string, double>& MutableCountersForDomain(FrameRecord& frame, PerformanceDomain domain);

    std::vector<PerformanceZoneSummary> SummarizeZones() const;
    std::vector<PerformanceCounterSummary> SummarizeCounters() const;
    std::vector<PerformanceSpike> SummarizeSpikes() const;

    bool enabled_ = false;
    bool paused_ = false;
    std::size_t capacity_ = 360;
    int nextFrameIndex_ = 1;
    bool frameActive_ = false;
    FrameRecord currentFrame_;
    std::vector<FrameRecord> frames_;
};

class PerformanceScope {
public:
    PerformanceScope(PerformanceProfiler* profiler, PerformanceDomain domain, const char* name);
    PerformanceScope(PerformanceProfiler* profiler, PerformanceDomain domain, const std::string& name);
    ~PerformanceScope();

    PerformanceScope(const PerformanceScope&) = delete;
    PerformanceScope& operator=(const PerformanceScope&) = delete;

private:
    PerformanceProfiler* profiler_ = nullptr;
    PerformanceDomain domain_ = PerformanceDomain::Editor;
    std::string name_;
    bool active_ = false;
    std::chrono::steady_clock::time_point started_;
};

} // namespace aine

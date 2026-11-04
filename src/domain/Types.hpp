#ifndef MEMTEST_DOMAIN_TYPES_HPP
#define MEMTEST_DOMAIN_TYPES_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace memtest {

enum class WorkloadKind { Concurrency, CpuLimit, MemoryPressure, SharedCounter };
enum class WorkerMode   { NativeThreads, RuntimeTasks };
enum class SyncMode     { None, Mutex, Atomic };
enum class MemoryLimitKind { None, AddressSpace, DataSegment };
enum class RunOutcome   { Completed, Stopped, MemoryLimitHit, CpuLimitHit, ExternalKill, Error };
enum class LimitStatus  { NotRequested, Applied, NotApplied, Unsupported };
enum class CompareParameter { Threads, MemoryBytes, CpuSeconds };
enum class RunState     { Idle, Validating, Starting, Running, Stopping, Collecting, Done };

struct SystemInfo {
    std::uint64_t total_ram = 0;
    std::uint64_t available_ram = 0;
    std::uint64_t swap_total = 0;
    int cores = 0;
};

struct ValidationIssue {
    std::string field;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    bool ok() const { return issues.empty(); }
};

struct CpuLimitConfig {
    std::optional<int> seconds;        // RLIMIT_CPU: soft=N, hard=N+1
    std::optional<int> quota_percent;  // cpu.max; extra fuera del MVP
};

struct ExperimentConfig {
    WorkloadKind workload = WorkloadKind::Concurrency;
    WorkerMode worker_mode = WorkerMode::NativeThreads;
    SyncMode sync_mode = SyncMode::None;
    int threads = 4;
    std::uint64_t work_units = 100000;
    std::uint64_t increments_per_thread = 100000;
    std::uint64_t memory_bytes = 0;
    std::uint64_t page_stride_bytes = 4096;
    int wall_timeout_ms = 5000;
    MemoryLimitKind memory_limit_kind = MemoryLimitKind::None;
    std::uint64_t memory_limit_bytes = 0;
    CpuLimitConfig cpu_limit;
};

struct MetricsSnapshot {
    std::chrono::milliseconds elapsed{0};
    std::uint64_t vm_size = 0;      // VmSize
    std::uint64_t vm_rss = 0;       // VmRSS
    std::uint64_t vm_hwm = 0;       // VmHWM
    std::uint64_t vm_swap = 0;      // VmSwap
    std::uint64_t rss_anon = 0;     // RssAnon
    std::uint64_t rss_file = 0;     // RssFile
    std::uint64_t rss_shmem = 0;    // RssShmem
    std::uint64_t minor_faults = 0; // minflt acumulado
    std::uint64_t major_faults = 0; // majflt acumulado
    double cpu_user_s = 0.0;
    double cpu_sys_s = 0.0;
    int threads = 0;                // Threads
    std::uint64_t voluntary_ctxt = 0;
    std::uint64_t involuntary_ctxt = 0;
    std::uint64_t sys_swap_total = 0;
    std::uint64_t sys_swap_free = 0;
    double load_1m = 0.0;
};

struct LimitReport {
    MemoryLimitKind memory_kind = MemoryLimitKind::None;
    LimitStatus memory_status = LimitStatus::NotRequested;
    std::uint64_t memory_applied = 0;

    LimitStatus cpu_seconds_status = LimitStatus::NotRequested;
    int cpu_seconds_applied = 0;

    LimitStatus cpu_quota_status = LimitStatus::NotRequested;
    int cpu_quota_applied = 0;
    std::uint64_t nr_throttled = 0;
    std::uint64_t throttled_usec = 0;
};

struct ExperimentResult {
    std::string run_id;
    ExperimentConfig config;
    LimitReport limits;
    RunOutcome outcome = RunOutcome::Error;
    std::string detail;

    MetricsSnapshot peak;
    MetricsSnapshot last;
    std::vector<MetricsSnapshot> series;

    std::uint64_t expected_value = 0;
    std::uint64_t observed_value = 0;
    std::chrono::milliseconds duration{0};
    int exit_code = 0;
    int term_signal = 0;
};

}  // namespace memtest

#endif  // MEMTEST_DOMAIN_TYPES_HPP

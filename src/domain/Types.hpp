#ifndef MEMTEST_DOMAIN_TYPES_HPP
#define MEMTEST_DOMAIN_TYPES_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace memtest {

// Modelo de dominio congelado (Arquitectura.md §4). Son tipos de datos puros,
// sin herencia y sin dependencias de plataforma.

enum class WorkloadKind { Concurrency, CpuLimit, MemoryPressure, SharedCounter };
enum class WorkerMode { NativeThreads, RuntimeTasks };
// Origen del presupuesto de trabajo de Concurrency. `Duration` mantiene el
// comportamiento historico (correr hasta el deadline); `TotalWork` reparte una
// cantidad fija de unidades entre las tareas.
enum class LoadMode { Duration, TotalWork };
enum class SyncMode { None, Mutex, Atomic };
enum class MemoryLimitKind { None, AddressSpace, DataSegment };
enum class RunOutcome { Completed, Stopped, MemoryLimitHit, CpuLimitHit, ExternalKill, Error };
enum class LimitStatus { NotRequested, Applied, NotApplied, Unsupported };
enum class CompareParameter { Threads, MemoryBytes, CpuSeconds };  // eje de la gráfica comparativa
enum class RunState { Idle, Validating, Starting, Running, Stopping, Collecting, Done };

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
    LoadMode load_mode = LoadMode::Duration;
    SyncMode sync_mode = SyncMode::None;
    int threads = 4;
    std::uint64_t work_units = 100000;
    std::uint64_t increments_per_thread = 100000;
    std::uint64_t memory_bytes = 0;
    std::uint64_t page_stride_bytes = 4096;
    // Fuerza el reemplazo de las paginas tocadas con MADV_PAGEOUT. Es una
    // peticion best-effort: solo tiene efecto si el sistema tiene SWAP.
    bool force_swap = false;
    int wall_timeout_ms = 5000;
    // Duración real. En LoadMode::Duration define la carga; en TotalWork es
    // solo un techo de seguridad. Si es 0 se conserva el modo histórico.
    int duration_ms = 0;
    MemoryLimitKind memory_limit_kind = MemoryLimitKind::None;
    std::uint64_t memory_limit_bytes = 0;
    CpuLimitConfig cpu_limit;
};

struct MetricsSnapshot {
    std::chrono::milliseconds elapsed{0};
    std::uint64_t vm_size = 0;       // VmSize
    std::uint64_t vm_rss = 0;        // VmRSS
    std::uint64_t vm_hwm = 0;        // VmHWM
    std::uint64_t vm_swap = 0;       // VmSwap
    std::uint64_t rss_anon = 0;      // RssAnon
    std::uint64_t rss_file = 0;      // RssFile
    std::uint64_t rss_shmem = 0;     // RssShmem
    std::uint64_t minor_faults = 0;  // minflt acumulado
    std::uint64_t major_faults = 0;  // majflt acumulado
    double cpu_user_s = 0.0;
    double cpu_sys_s = 0.0;
    int threads = 0;  // Threads
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

// Estado global del sistema en un instante. A diferencia de MetricsSnapshot,
// que describe al proceso worker, este snapshot describe la máquina completa:
// CPU, RAM, SWAP, carga, fallos de página y cambios de contexto.
struct SystemSnapshot {
    std::chrono::milliseconds elapsed{0};
    double cpu_total_percent = 0.0;
    double cpu_user_percent = 0.0;
    double cpu_system_percent = 0.0;
    double cpu_iowait_percent = 0.0;

    std::uint64_t mem_total = 0;
    std::uint64_t mem_available = 0;
    std::uint64_t mem_used = 0;  // total - available
    std::uint64_t mem_free = 0;

    std::uint64_t swap_total = 0;
    std::uint64_t swap_free = 0;
    std::uint64_t swap_used = 0;  // total - free
    bool swap_available = false;

    double load_1m = 0.0;
    double load_5m = 0.0;
    double load_15m = 0.0;

    std::uint64_t procs_running = 0;
    std::uint64_t procs_blocked = 0;
    std::uint64_t processes = 0;

    std::uint64_t ctxt = 0;
    std::uint64_t ctxt_per_second = 0;

    std::uint64_t minor_faults = 0;
    std::uint64_t major_faults = 0;
    std::uint64_t minor_faults_per_second = 0;
    std::uint64_t major_faults_per_second = 0;

    std::uint64_t swap_in = 0;
    std::uint64_t swap_out = 0;
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

    // Impacto sobre el sistema: línea base al recibir READY, pico durante la
    // carga y última muestra. Los deltas se calculan al terminar.
    SystemSnapshot system_baseline;
    SystemSnapshot system_peak;
    SystemSnapshot system_last;
    std::uint64_t system_ram_delta = 0;
    std::uint64_t system_swap_delta = 0;
    double system_cpu_peak_percent = 0.0;
    std::uint64_t system_minor_faults_delta = 0;
    std::uint64_t system_major_faults_delta = 0;

    std::uint64_t expected_value = 0;
    std::uint64_t observed_value = 0;
    std::chrono::milliseconds duration{0};
    int exit_code = 0;
    int term_signal = 0;
};

}  // namespace memtest

#endif  // MEMTEST_DOMAIN_TYPES_HPP

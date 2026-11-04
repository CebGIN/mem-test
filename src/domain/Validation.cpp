#include "domain/Validation.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace memtest {
namespace {

constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;
// Si el sistema no reporta RAM se asume 4 GiB: el tope queda en 1 GiB, que es
// el valor absoluto del guardrail y no depende de la máquina.
constexpr std::uint64_t kFallbackTotalRam = 4ull * kGiB;
constexpr int kMaxWallTimeoutMs = 120 * 1000;
constexpr int kMinCpuSeconds = 1;
constexpr int kMaxCpuSeconds = 120;

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

std::uint64_t to_mib(std::uint64_t bytes) { return bytes / kMiB; }

void add(ValidationReport& report, const char* field, std::string message) {
    report.issues.push_back({field, std::move(message)});
}

bool uses_threads(WorkloadKind kind) { return kind != WorkloadKind::MemoryPressure; }

bool uses_work_units(WorkloadKind kind) {
    return kind == WorkloadKind::Concurrency || kind == WorkloadKind::CpuLimit;
}

}  // namespace

GuardrailLimits guardrails_from(const SystemInfo& info) {
    const std::uint64_t total_ram = info.total_ram > 0 ? info.total_ram : kFallbackTotalRam;
    const int cores = info.cores > 0 ? info.cores : 1;

    GuardrailLimits limits;
    limits.max_memory_bytes = std::min<std::uint64_t>(kGiB, total_ram / 4);
    limits.max_threads = std::min(8 * cores, 64);
    limits.max_wall_timeout_ms = kMaxWallTimeoutMs;
    limits.min_cpu_seconds = kMinCpuSeconds;
    limits.max_cpu_seconds = kMaxCpuSeconds;
    return limits;
}

ValidationReport validate(const ExperimentConfig& config, const GuardrailLimits& limits) {
    ValidationReport report;

    if (uses_threads(config.workload)) {
        if (config.threads < 1) {
            add(report, "threads", "Debe haber al menos un hilo.");
        } else if (config.threads > limits.max_threads) {
            add(report, "threads",
                "Supera el máximo de " + std::to_string(limits.max_threads) +
                    " hilos para esta máquina.");
        }
    }

    if (uses_work_units(config.workload) && config.work_units < 1) {
        add(report, "work_units", "Debe ejecutarse al menos una unidad de trabajo.");
    }

    if (config.workload == WorkloadKind::SharedCounter && config.increments_per_thread < 1) {
        add(report, "increments", "Debe hacerse al menos un incremento por tarea.");
    }

    if (config.wall_timeout_ms < 1) {
        add(report, "wall_timeout_ms", "La duración de pared debe ser mayor que cero.");
    } else if (config.wall_timeout_ms > limits.max_wall_timeout_ms) {
        add(report, "wall_timeout_ms",
            "No puede superar " + std::to_string(limits.max_wall_timeout_ms) + " ms.");
    }

    if (config.workload == WorkloadKind::MemoryPressure &&
        config.memory_bytes > limits.max_memory_bytes) {
        add(report, "memory_bytes",
            "Supera el tope de " + std::to_string(to_mib(limits.max_memory_bytes)) + " MiB.");
    }

    if (config.memory_limit_kind != MemoryLimitKind::None) {
        if (config.memory_limit_bytes == 0) {
            add(report, "memory_limit_bytes", "El límite debe ser mayor que cero.");
        } else {
            if (config.memory_limit_bytes > limits.max_memory_bytes) {
                add(report, "memory_limit_bytes",
                    "Supera el tope de " + std::to_string(to_mib(limits.max_memory_bytes)) +
                        " MiB.");
            }
            if (config.memory_bytes > config.memory_limit_bytes) {
                add(report, "memory_limit_bytes",
                    "El límite es menor que la memoria a reservar; el worker no podrá "
                    "hacer el mapeo.");
            }
        }
    }

    if (config.cpu_limit.seconds.has_value()) {
        const int seconds = *config.cpu_limit.seconds;
        if (seconds < limits.min_cpu_seconds) {
            add(report, "cpu_seconds", "Debe ser al menos 1 segundo.");
        } else if (seconds > limits.max_cpu_seconds) {
            add(report, "cpu_seconds",
                "No puede superar " + std::to_string(limits.max_cpu_seconds) + " segundos.");
        }
    }

    if (config.cpu_limit.quota_percent.has_value()) {
        const int percent = *config.cpu_limit.quota_percent;
        if (percent < 1 || percent > 100) {
            add(report, "cpu_quota", "La cuota debe estar entre 1 y 100 por ciento.");
        }
    }

    if (config.page_stride_bytes == 0) {
        add(report, "page_stride", "El paso entre páginas debe ser mayor que cero.");
    }

    return report;
}

}  // namespace memtest

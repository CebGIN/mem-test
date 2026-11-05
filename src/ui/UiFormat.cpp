#include "ui/UiFormat.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace memtest::ui {
namespace {

std::string fixed_number(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

// Memoria con la unidad más legible: MiB por debajo de 1 GiB y GiB encima.
std::string memory_text(std::uint64_t bytes) {
    return bytes >= kGiB ? gib(bytes) : mib(bytes);
}

std::string compact_duration(std::chrono::milliseconds duration) {
    return fixed_number(static_cast<double>(duration.count()) / 1000.0, 1) + "s";
}

std::string short_workload(WorkloadKind kind) {
    switch (kind) {
        case WorkloadKind::Concurrency: return "Conc.";
        case WorkloadKind::CpuLimit: return "CPU";
        case WorkloadKind::MemoryPressure: return "Mem.";
        case WorkloadKind::SharedCounter: return "Com.";
    }
    return "?";
}

std::string short_sync(SyncMode mode) {
    switch (mode) {
        case SyncMode::None: return "sin sync";
        case SyncMode::Mutex: return "Mutex";
        case SyncMode::Atomic: return "Atómico";
    }
    return "?";
}

std::string short_outcome(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Completed: return "OK";
        case RunOutcome::Stopped: return "Det.";
        case RunOutcome::MemoryLimitHit: return "Mem!";
        case RunOutcome::CpuLimitHit: return "CPU!";
        case RunOutcome::ExternalKill: return "Kill";
        case RunOutcome::Error: return "Error";
    }
    return "?";
}

// Parámetro que distingue la corrida en una fila del historial.
std::string short_parameter(const ExperimentResult& result) {
    const ExperimentConfig& config = result.config;
    switch (config.workload) {
        case WorkloadKind::MemoryPressure:
            return std::to_string(config.memory_bytes / kMiB) + "MiB";
        case WorkloadKind::SharedCounter:
            return std::to_string(config.threads) + "h " +
                   short_sync(config.sync_mode);
        default:
            return std::to_string(config.threads) + "h " +
                   (config.worker_mode == WorkerMode::NativeThreads ? "nat"
                                                                    : "task");
    }
}

std::string delta_text(double value, const char* unit, int precision) {
    if (std::abs(value) < 0.05) {
        return {};
    }
    return " (" + std::string(value > 0.0 ? "+" : "-") +
           fixed_number(std::abs(value), precision) + " " + unit + ")";
}

std::string memory_delta(std::uint64_t value, std::uint64_t base) {
    const double mib_value =
        (static_cast<double>(value) - static_cast<double>(base)) /
        static_cast<double>(kMiB);
    return delta_text(mib_value, "MiB", 1);
}

std::string memory_limit_line(const LimitReport& limits) {
    switch (limits.memory_status) {
        case LimitStatus::NotRequested:
            return "sin límite";
        case LimitStatus::Applied:
            return memory_limit_text(limits.memory_kind) + " " +
                   memory_text(limits.memory_applied);
        case LimitStatus::NotApplied:
            return memory_limit_text(limits.memory_kind) + " no aplicado";
        case LimitStatus::Unsupported:
            return "no soportado";
    }
    return "?";
}

std::string cpu_limit_line(const LimitReport& limits) {
    switch (limits.cpu_quota_status) {
        case LimitStatus::Applied: {
            std::string text =
                "cuota " + std::to_string(limits.cpu_quota_applied) + "%";
            if (limits.throttled_usec > 0) {
                text += " · throttled " +
                        seconds_text(static_cast<double>(
                                         limits.throttled_usec) /
                                     1000000.0) +
                        " (" + std::to_string(limits.nr_throttled) + ")";
            }
            return text;
        }
        case LimitStatus::NotApplied:
            return "cuota no aplicada";
        case LimitStatus::Unsupported:
            return "cuota no soportada";
        case LimitStatus::NotRequested:
            break;
    }

    switch (limits.cpu_seconds_status) {
        case LimitStatus::NotRequested:
            return "sin límite";
        case LimitStatus::Applied:
            return std::to_string(limits.cpu_seconds_applied) + " s";
        case LimitStatus::NotApplied:
            return "no aplicado";
        case LimitStatus::Unsupported:
            return "no soportado";
    }
    return "?";
}

}  // namespace

std::string mib(std::uint64_t bytes) {
    return fixed_number(static_cast<double>(bytes) / static_cast<double>(kMiB), 1) + " MiB";
}

std::string gib(std::uint64_t bytes) {
    return fixed_number(static_cast<double>(bytes) / static_cast<double>(kGiB), 1) + " GiB";
}

std::string seconds_text(double seconds) {
    return fixed_number(seconds, 2) + " s";
}

std::string duration_text(std::chrono::milliseconds duration) {
    return seconds_text(static_cast<double>(duration.count()) / 1000.0);
}

std::string state_text(RunState state) {
    switch (state) {
        case RunState::Idle: return "Inactivo";
        case RunState::Validating: return "Validando";
        case RunState::Starting: return "Iniciando";
        case RunState::Running: return "Ejecutando";
        case RunState::Stopping: return "Deteniendo";
        case RunState::Collecting: return "Recolectando";
        case RunState::Done: return "Terminada";
    }
    return "Desconocido";
}

std::string outcome_text(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Completed: return "Completada";
        case RunOutcome::Stopped: return "Detenida";
        case RunOutcome::MemoryLimitHit: return "Límite de memoria";
        case RunOutcome::CpuLimitHit: return "Límite de CPU";
        case RunOutcome::ExternalKill: return "Muerte externa";
        case RunOutcome::Error: return "Error";
    }
    return "Desconocido";
}

const char* outcome_color(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Completed: return "GREEN";
        case RunOutcome::Stopped: return "CYAN";
        case RunOutcome::MemoryLimitHit:
        case RunOutcome::CpuLimitHit: return "YELLOW";
        case RunOutcome::ExternalKill:
        case RunOutcome::Error: return "RED";
    }
    return "WHITE";
}

std::string workload_text(WorkloadKind kind) {
    switch (kind) {
        case WorkloadKind::Concurrency: return "Concurrencia";
        case WorkloadKind::CpuLimit: return "Límite de CPU";
        case WorkloadKind::MemoryPressure: return "Presión de memoria";
        case WorkloadKind::SharedCounter: return "Acceso compartido";
    }
    return "Desconocida";
}

std::string worker_mode_text(WorkerMode mode) {
    switch (mode) {
        case WorkerMode::NativeThreads: return "Hilos nativos";
        case WorkerMode::RuntimeTasks: return "Tareas runtime";
    }
    return "Desconocido";
}

std::string sync_mode_text(SyncMode mode) {
    switch (mode) {
        case SyncMode::None: return "Sin sincronización";
        case SyncMode::Mutex: return "Mutex";
        case SyncMode::Atomic: return "Atómico";
    }
    return "Desconocido";
}

std::string memory_limit_text(MemoryLimitKind kind) {
    switch (kind) {
        case MemoryLimitKind::None: return "sin límite";
        case MemoryLimitKind::AddressSpace: return "AS";
        case MemoryLimitKind::DataSegment: return "DATA";
    }
    return "desconocido";
}

std::string limit_status_text(LimitStatus status) {
    switch (status) {
        case LimitStatus::NotRequested: return "sin límite";
        case LimitStatus::Applied: return "aplicado";
        case LimitStatus::NotApplied: return "no aplicado";
        case LimitStatus::Unsupported: return "no soportado";
    }
    return "desconocido";
}

std::string history_row(const ExperimentResult& result) {
    std::ostringstream out;
    out << result.run_id << " · " << short_workload(result.config.workload)
        << " · " << short_parameter(result) << " · "
        << compact_duration(result.duration) << " · "
        << short_outcome(result.outcome);
    return out.str();
}

std::string detail_title(const ExperimentResult& result) {
    return result.run_id + " · " + workload_text(result.config.workload);
}

std::string detail_fields(const ExperimentResult& result,
                          const ExperimentResult* baseline) {
    const ExperimentConfig& config = result.config;
    const MetricsSnapshot& peak = result.peak;
    const bool has_baseline = baseline != nullptr;

    // El Label de Guayaba colapsa los espacios repetidos al envolver el texto,
    // así que las columnas se separan con dos puntos y no con padding.
    std::ostringstream out;

    const std::string duration = duration_text(result.duration);
    if (config.workload == WorkloadKind::MemoryPressure) {
        out << "Reserva: " << (config.memory_bytes / kMiB) << " MiB · "
            << duration << " · Pág. " << result.observed_value << "/"
            << result.expected_value << '\n';
    } else if (config.workload == WorkloadKind::SharedCounter) {
        out << "Hilos: " << config.threads << " · "
            << short_sync(config.sync_mode) << " · " << duration << '\n';
    } else {
        out << "Hilos: " << config.threads << " · "
            << (config.worker_mode == WorkerMode::NativeThreads ? "Nativo"
                                                                : "Tareas")
            << " · " << duration;
        if (config.load_mode == LoadMode::TotalWork) {
            std::string total = std::to_string(config.work_units);
            if (config.work_units >= 1000000 &&
                config.work_units % 1000000 == 0) {
                total = std::to_string(config.work_units / 1000000) + "M";
            }
            out << " · Total " << total;
        }
        out << '\n';
    }

    out << "Resultado: " << outcome_text(result.outcome) << '\n';

    // VmHWM conserva el maximo aunque el muestreo no llegue a verlo, algo
    // importante cuando MADV_PAGEOUT baja el RSS antes de la proxima muestra.
    const std::uint64_t rss_peak = std::max(peak.vm_rss, peak.vm_hwm);
    out << "RSS pico: " << memory_text(rss_peak);
    if (has_baseline) {
        const std::uint64_t rss_base =
            std::max(baseline->peak.vm_rss, baseline->peak.vm_hwm);
        out << memory_delta(rss_peak, rss_base);
    }
    out << '\n';

    out << "Virtual: " << memory_text(peak.vm_size);
    if (has_baseline) {
        out << memory_delta(peak.vm_size, baseline->peak.vm_size);
    }
    out << '\n';

    out << "SWAP carga: ";
    if (peak.sys_swap_total == 0) {
        out << "no disponible";
    } else {
        out << memory_text(peak.vm_swap);
    }
    out << '\n';

    out << "Fallos: " << peak.minor_faults << " men / " << peak.major_faults
        << " may\n";

    out << "CPU carga: " << seconds_text(peak.cpu_user_s + peak.cpu_sys_s);
    if (has_baseline) {
        const double cpu = peak.cpu_user_s + peak.cpu_sys_s;
        const double base_cpu =
            baseline->peak.cpu_user_s + baseline->peak.cpu_sys_s;
        out << delta_text(cpu - base_cpu, "s", 2);
    }
    if (config.workload == WorkloadKind::SharedCounter) {
        out << " · Ctxt " << peak.voluntary_ctxt << "/"
            << peak.involuntary_ctxt;
    }
    out << '\n';

    out << "Sist.: CPU "
        << static_cast<int>(std::lround(result.system_cpu_peak_percent))
        << "% · ΔRAM " << memory_text(result.system_ram_delta) << '\n';

    out << "SWAP sist.: ";
    if (result.system_baseline.swap_total == 0) {
        out << "no disponible";
    } else {
        out << memory_text(result.system_peak.swap_used) << "/"
            << memory_text(result.system_baseline.swap_total);
    }
    out << '\n';

    out << "Lím. mem: " << memory_limit_line(result.limits) << '\n';
    out << "Lím. cpu: " << cpu_limit_line(result.limits) << '\n';

    if (config.workload == WorkloadKind::SharedCounter) {
        const std::uint64_t lost =
            result.expected_value > result.observed_value
                ? result.expected_value - result.observed_value
                : 0;
        const double percent =
            result.expected_value > 0
                ? 100.0 * static_cast<double>(lost) /
                      static_cast<double>(result.expected_value)
                : 0.0;
        out << "Perdidas: " << lost << " ("
            << fixed_number(percent, 1) << "%)";
    }

    std::string text = out.str();
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

std::string detail_block(const ExperimentResult& result,
                         const ExperimentResult* baseline) {
    return detail_title(result) + "\n" + detail_fields(result, baseline);
}

std::string limits_summary(const ExperimentResult& result) {
    std::ostringstream out;
    out << "Límites: memoria " << limit_status_text(result.limits.memory_status);
    if (result.limits.memory_status == LimitStatus::Applied) {
        out << " (" << memory_limit_text(result.limits.memory_kind) << " "
            << mib(result.limits.memory_applied) << ")";
    }
    out << " · CPU ";
    if (result.limits.cpu_quota_status != LimitStatus::NotRequested) {
        out << limit_status_text(result.limits.cpu_quota_status);
        if (result.limits.cpu_quota_status == LimitStatus::Applied) {
            out << " (" << result.limits.cpu_quota_applied << "%)";
        }
    } else {
        out << limit_status_text(result.limits.cpu_seconds_status);
        if (result.limits.cpu_seconds_status == LimitStatus::Applied) {
            out << " (" << result.limits.cpu_seconds_applied << " s)";
        }
    }
    return out.str();
}

std::vector<std::string> workload_items() {
    return {"Concurrencia", "Límite de CPU", "Presión de memoria", "Acceso compartido"};
}

std::vector<std::string> worker_mode_items() {
    return {"Nativos", "Tareas"};
}

std::vector<std::string> sync_mode_items() {
    return {"Ninguna", "Mutex", "Atómico"};
}

std::vector<std::string> memory_limit_items() {
    return {"Sin límite", "AS (virtual)", "DATA (datos)"};
}

std::vector<std::string> cpu_limit_mode_items() {
    return {"Tiempo total", "Cuota"};
}

}  // namespace memtest::ui

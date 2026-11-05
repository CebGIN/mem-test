#include "ui/UiFormat.hpp"

#include <iomanip>
#include <sstream>

namespace memtest::ui {
namespace {

std::string fixed_number(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
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
    out << result.run_id << " · " << outcome_text(result.outcome) << " · "
        << duration_text(result.duration) << " · RSS " << mib(result.peak.vm_rss);
    return out.str();
}

std::string limits_summary(const ExperimentResult& result) {
    std::ostringstream out;
    out << "Límites: memoria " << limit_status_text(result.limits.memory_status);
    if (result.limits.memory_status == LimitStatus::Applied) {
        out << " (" << memory_limit_text(result.limits.memory_kind) << " "
            << mib(result.limits.memory_applied) << ")";
    }
    out << " · CPU " << limit_status_text(result.limits.cpu_seconds_status);
    if (result.limits.cpu_seconds_status == LimitStatus::Applied) {
        out << " (" << result.limits.cpu_seconds_applied << " s)";
    }
    if (result.limits.cpu_quota_status == LimitStatus::Unsupported) {
        out << " · cuota no soportada";
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
    return {"Sin límite", "AS", "DATA"};
}

}  // namespace memtest::ui

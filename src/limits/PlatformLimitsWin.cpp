#include "limits/PlatformLimits.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <limits>

// Windows no tiene setrlimit. El equivalente es un Job Object: el proceso se
// asigna a sí mismo a un job con límites. Un job solo admite un conjunto de
// límites a la vez, así que memoria y CPU comparten el mismo objeto y se
// reescribe la estructura completa en cada llamada.

namespace memtest::limits::detail {

namespace {

struct JobState {
    HANDLE job = nullptr;
    bool assigned = false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
};

JobState& state() {
    static JobState s;  // el handle vive hasta que el proceso termina
    return s;
}

void add_note(std::string& notes, const std::string& text) {
    if (!notes.empty()) {
        notes += "; ";
    }
    notes += text;
}

std::string last_error_text(const char* what) {
    return std::string(what) + " fallo (GetLastError=" +
           std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
}

// Crea el job la primera vez, mete al proceso actual y vuelca los límites
// acumulados. Devuelve false y rellena `notes` si algo falla.
bool commit_job(std::string& notes) {
    JobState& s = state();
    if (s.job == nullptr) {
        s.job = CreateJobObjectW(nullptr, nullptr);
        if (s.job == nullptr) {
            add_note(notes, last_error_text("CreateJobObject"));
            return false;
        }
    }
    if (!SetInformationJobObject(s.job, JobObjectExtendedLimitInformation,
                                 &s.info, sizeof(s.info))) {
        add_note(notes, last_error_text("SetInformationJobObject"));
        return false;
    }
    if (!s.assigned) {
        // Falla si el proceso ya está en un job que no admite anidamiento
        // (Windows anterior a 8) o si la politica del entorno lo impide.
        if (!AssignProcessToJobObject(s.job, GetCurrentProcess())) {
            add_note(notes, last_error_text("AssignProcessToJobObject"));
            return false;
        }
        s.assigned = true;
    }
    return true;
}

}  // namespace

void apply_memory_limit(MemoryLimitKind kind, std::uint64_t bytes,
                        LimitReport& report, std::string& notes) {
    if (bytes > static_cast<std::uint64_t>(std::numeric_limits<SIZE_T>::max())) {
        report.memory_status = LimitStatus::NotApplied;
        add_note(notes, "limite de memoria fuera de rango para este sistema");
        return;
    }

    JobState& s = state();
    const JOBOBJECT_EXTENDED_LIMIT_INFORMATION previous = s.info;
    s.info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    s.info.ProcessMemoryLimit = static_cast<SIZE_T>(bytes);

    if (commit_job(notes)) {
        report.memory_status = LimitStatus::Applied;
        report.memory_applied = bytes;
        // Windows limita memoria COMPROMETIDA, no espacio de direcciones
        // reservado: AS y DATA se comportan igual. Se deja constancia.
        add_note(notes, kind == MemoryLimitKind::AddressSpace
                            ? "Windows: AddressSpace se aplica como limite de "
                              "memoria comprometida (job object)"
                            : "Windows: DataSegment se aplica como limite de "
                              "memoria comprometida (job object)");
    } else {
        s.info = previous;
        report.memory_status = LimitStatus::NotApplied;
    }
}

void apply_cpu_seconds_limit(int seconds, LimitReport& report,
                             std::string& notes) {
    JobState& s = state();
    const JOBOBJECT_EXTENDED_LIMIT_INFORMATION previous = s.info;
    s.info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
    // Unidades de 100 ns. Límite duro N + 1; el blando N lo avisa
    // install_cpu_limit_notifier(). Windows cuenta solo tiempo de usuario.
    s.info.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart =
        static_cast<LONGLONG>(seconds + 1) * 10000000LL;

    if (commit_job(notes)) {
        report.cpu_seconds_status = LimitStatus::Applied;
        report.cpu_seconds_applied = seconds;
        add_note(notes,
                 "Windows: el limite duro (N+1) cuenta solo CPU de usuario");
    } else {
        s.info = previous;
        report.cpu_seconds_status = LimitStatus::NotApplied;
    }
}

}  // namespace memtest::limits::detail

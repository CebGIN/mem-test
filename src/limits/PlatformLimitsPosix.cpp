#include "limits/PlatformLimits.hpp"

#include <sys/resource.h>

#include <cerrno>
#include <cstring>

namespace memtest::limits::detail {

namespace {

// Aplica {soft, hard} a `resource` y comprueba con getrlimit que el kernel
// dejó lo pedido. Devuelve true si todo coincide; si no, `error` explica.
bool set_and_verify(int resource, rlim_t soft, rlim_t hard, std::string& error) {
    struct rlimit wanted;
    wanted.rlim_cur = soft;
    wanted.rlim_max = hard;
    if (setrlimit(resource, &wanted) != 0) {
        error = std::strerror(errno);
        return false;
    }
    struct rlimit actual;
    if (getrlimit(resource, &actual) != 0) {
        error = std::strerror(errno);
        return false;
    }
    if (actual.rlim_cur != soft || actual.rlim_max != hard) {
        error = "el kernel dejo un valor distinto al pedido";
        return false;
    }
    return true;
}

void add_note(std::string& notes, const std::string& text) {
    if (!notes.empty()) {
        notes += "; ";
    }
    notes += text;
}

}  // namespace

void apply_memory_limit(MemoryLimitKind kind, std::uint64_t bytes,
                        LimitReport& report, std::string& notes) {
    const char* name = (kind == MemoryLimitKind::AddressSpace) ? "RLIMIT_AS"
                                                               : "RLIMIT_DATA";
    const int resource =
        (kind == MemoryLimitKind::AddressSpace) ? RLIMIT_AS : RLIMIT_DATA;

    if (bytes >= static_cast<std::uint64_t>(RLIM_INFINITY)) {
        report.memory_status = LimitStatus::NotApplied;
        add_note(notes, std::string(name) + ": valor fuera de rango");
        return;
    }

    // Memoria: soft = hard, para que el proceso no pueda subir su propio tope.
    const rlim_t limit = static_cast<rlim_t>(bytes);
    std::string error;
    if (set_and_verify(resource, limit, limit, error)) {
        report.memory_status = LimitStatus::Applied;
        report.memory_applied = bytes;
    } else {
        report.memory_status = LimitStatus::NotApplied;
        add_note(notes, std::string("setrlimit(") + name + ") fallo: " + error);
    }
}

void apply_cpu_seconds_limit(int seconds, LimitReport& report,
                             std::string& notes) {
    // CPU: soft = N avisa con SIGXCPU; hard = N + 1 mata con SIGKILL si el
    // proceso no reacciona al aviso.
    const rlim_t soft = static_cast<rlim_t>(seconds);
    const rlim_t hard = soft + 1;
    std::string error;
    if (set_and_verify(RLIMIT_CPU, soft, hard, error)) {
        report.cpu_seconds_status = LimitStatus::Applied;
        report.cpu_seconds_applied = seconds;
    } else {
        report.cpu_seconds_status = LimitStatus::NotApplied;
        add_note(notes, "setrlimit(RLIMIT_CPU) fallo: " + error);
    }
}

}  // namespace memtest::limits::detail

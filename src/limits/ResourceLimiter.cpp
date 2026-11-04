#include "limits/ResourceLimiter.hpp"

#include "limits/PlatformLimits.hpp"

namespace memtest::limits {

namespace {

void add_note(std::string& notes, const std::string& text) {
    if (!notes.empty()) {
        notes += "; ";
    }
    notes += text;
}

}  // namespace

LimitReport ResourceLimiter::apply(const ExperimentConfig& config,
                                   std::string* detail) const {
    LimitReport report;
    std::string notes;

    report.memory_kind = config.memory_limit_kind;
    if (config.memory_limit_kind != MemoryLimitKind::None) {
        if (config.memory_limit_bytes == 0) {
            report.memory_status = LimitStatus::NotApplied;
            add_note(notes, "limite de memoria 0: no se aplico");
        } else {
            detail::apply_memory_limit(config.memory_limit_kind,
                                       config.memory_limit_bytes, report, notes);
        }
    }

    if (config.cpu_limit.seconds.has_value()) {
        const int seconds = *config.cpu_limit.seconds;
        if (seconds < 1) {
            report.cpu_seconds_status = LimitStatus::NotApplied;
            add_note(notes, "limite de CPU < 1 s: no se aplico");
        } else {
            detail::apply_cpu_seconds_limit(seconds, report, notes);
        }
    }

    // La cuota (cgroup v2 cpu.max) está fuera del MVP: se pide, pero no se
    // aplica, y el resultado lo dice con claridad.
    if (config.cpu_limit.quota_percent.has_value()) {
        report.cpu_quota_status = LimitStatus::Unsupported;
        add_note(notes, "cuota de CPU fuera del MVP");
    }

    if (detail != nullptr) {
        *detail = notes;
    }
    return report;
}

}  // namespace memtest::limits

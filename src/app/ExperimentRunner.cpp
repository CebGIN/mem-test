#include "app/ExperimentRunner.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

#include "metrics/IMetricsSource.hpp"
#include "process/WorkerProtocol.hpp"

namespace memtest {
namespace {

using Clock = std::chrono::steady_clock;

std::atomic<std::uint64_t> g_next_run_id{1};

std::string next_run_id() {
    return "run-" + std::to_string(g_next_run_id.fetch_add(1));
}

void accumulate_peak(MetricsSnapshot& peak, const MetricsSnapshot& sample) {
    peak.elapsed = std::max(peak.elapsed, sample.elapsed);
    peak.vm_size = std::max(peak.vm_size, sample.vm_size);
    peak.vm_rss = std::max(peak.vm_rss, sample.vm_rss);
    peak.vm_hwm = std::max(peak.vm_hwm, sample.vm_hwm);
    peak.vm_swap = std::max(peak.vm_swap, sample.vm_swap);
    peak.rss_anon = std::max(peak.rss_anon, sample.rss_anon);
    peak.rss_file = std::max(peak.rss_file, sample.rss_file);
    peak.rss_shmem = std::max(peak.rss_shmem, sample.rss_shmem);
    peak.minor_faults = std::max(peak.minor_faults, sample.minor_faults);
    peak.major_faults = std::max(peak.major_faults, sample.major_faults);
    peak.cpu_user_s = std::max(peak.cpu_user_s, sample.cpu_user_s);
    peak.cpu_sys_s = std::max(peak.cpu_sys_s, sample.cpu_sys_s);
    peak.threads = std::max(peak.threads, sample.threads);
    peak.voluntary_ctxt = std::max(peak.voluntary_ctxt, sample.voluntary_ctxt);
    peak.involuntary_ctxt =
        std::max(peak.involuntary_ctxt, sample.involuntary_ctxt);
    peak.sys_swap_total = std::max(peak.sys_swap_total, sample.sys_swap_total);
    peak.sys_swap_free = std::max(peak.sys_swap_free, sample.sys_swap_free);
    peak.load_1m = std::max(peak.load_1m, sample.load_1m);
}

const std::string* field(const WorkerEvent& event, const char* key) {
    const auto it = event.fields.find(key);
    return it == event.fields.end() ? nullptr : &it->second;
}

void apply_limits_event(const WorkerEvent& event, LimitReport& report) {
    if (const std::string* value = field(event, "memory")) {
        const std::size_t colon = value->find(':');
        const std::string_view status_text =
            std::string_view(*value).substr(0, colon);
        if (const auto status = parse_limit_status(status_text)) {
            report.memory_status = *status;
        }
        if (colon != std::string::npos) {
            const std::string number = value->substr(colon + 1);
            std::uint64_t bytes = 0;
            const auto parsed = std::from_chars(number.data(),
                                                number.data() + number.size(),
                                                bytes);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == number.data() + number.size()) {
                report.memory_applied = bytes;
            }
        }
    }
    if (const std::string* value = field(event, "cpu_seconds")) {
        const std::size_t colon = value->find(':');
        const std::string_view status_text =
            std::string_view(*value).substr(0, colon);
        if (const auto status = parse_limit_status(status_text)) {
            report.cpu_seconds_status = *status;
        }
        if (colon != std::string::npos) {
            const std::string number = value->substr(colon + 1);
            int seconds = 0;
            const auto parsed = std::from_chars(number.data(),
                                                number.data() + number.size(),
                                                seconds);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == number.data() + number.size()) {
                report.cpu_seconds_applied = seconds;
            }
        }
    }
    if (const std::string* value = field(event, "cpu_quota")) {
        const std::size_t colon = value->find(':');
        const std::string_view status_text =
            std::string_view(*value).substr(0, colon);
        if (const auto status = parse_limit_status(status_text)) {
            report.cpu_quota_status = *status;
        }
        if (colon != std::string::npos) {
            const std::string number = value->substr(colon + 1);
            int percent = 0;
            const auto parsed = std::from_chars(number.data(),
                                                number.data() + number.size(),
                                                percent);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == number.data() + number.size()) {
                report.cpu_quota_applied = percent;
            }
        }
    }
}

std::optional<std::uint64_t> u64_field(const WorkerEvent& event,
                                       const char* key) {
    const std::string* value = field(event, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value->data(),
                                        value->data() + value->size(), parsed);
    if (result.ec != std::errc{} ||
        result.ptr != value->data() + value->size()) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace

ExperimentRunner::ExperimentRunner(IMetricsSource& metrics,
                                   IProcessSupervisor& supervisor)
    : metrics_(metrics), supervisor_(supervisor) {}

ExperimentResult ExperimentRunner::run(const ExperimentConfig& config,
                                       const CancellationToken& cancel,
                                       SampleCallback on_sample,
                                       EventCallback on_event) {
    ExperimentResult result;
    result.run_id = next_run_id();
    result.config = config;
    result.limits.memory_kind = config.memory_limit_kind;

    std::string spawn_error;
    if (!supervisor_.spawn(config, &spawn_error)) {
        result.outcome = RunOutcome::Error;
        result.detail = spawn_error.empty() ? "no se pudo lanzar el worker"
                                            : spawn_error;
        result.exit_code = 13;
        return result;
    }

    const auto started = Clock::now();
    auto next_sample = started;
    auto stop_at = started;
    bool stop_sent = false;
    bool escalated = false;
    bool timed_out = false;
    bool got_result = false;

    auto process_event = [&](const WorkerEvent& event) {
        if (on_event) {
            on_event(event);
        }
        switch (event.type) {
            case WorkerEvent::Type::Limits:
                apply_limits_event(event, result.limits);
                break;
            case WorkerEvent::Type::Result: {
                if (const std::string* value = field(event, "outcome")) {
                    if (const auto outcome = parse_run_outcome(*value)) {
                        result.outcome = *outcome;
                        got_result = true;
                    }
                }
                if (const auto expected = u64_field(event, "expected")) {
                    result.expected_value = *expected;
                }
                if (const auto observed = u64_field(event, "observed")) {
                    result.observed_value = *observed;
                }
                if (const std::string* detail = field(event, "detail")) {
                    result.detail = *detail;
                }
                break;
            }
            case WorkerEvent::Type::Error: {
                result.outcome = RunOutcome::Error;
                if (const std::string* detail = field(event, "detail")) {
                    result.detail = *detail;
                } else if (const std::string* code = field(event, "code")) {
                    result.detail = "worker error: " + *code;
                }
                break;
            }
            case WorkerEvent::Type::Hello:
            case WorkerEvent::Type::Ready:
            case WorkerEvent::Type::Progress:
            case WorkerEvent::Type::End:
                break;
        }
    };

    for (;;) {
        for (const WorkerEvent& event : supervisor_.poll_events()) {
            process_event(event);
        }

        const auto now = Clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - started);

        if (supervisor_.pid() > 0 && now >= next_sample) {
            MetricsSnapshot sample;
            sample.elapsed = elapsed;
            if (metrics_.sample(supervisor_.pid(), sample)) {
                sample.elapsed = elapsed;
                result.series.push_back(sample);
                if (result.series.size() > 2000) {
                    result.series.erase(result.series.begin());
                }
                accumulate_peak(result.peak, sample);
                result.last = sample;
                if (on_sample) {
                    on_sample(sample);
                }
            }
            next_sample = now + std::chrono::milliseconds(250);
        }

        if (!stop_sent && elapsed.count() >= config.wall_timeout_ms) {
            timed_out = true;
            stop_sent = true;
            stop_at = now;
            supervisor_.request_stop();
        }
        if (!stop_sent && cancel.cancelled()) {
            stop_sent = true;
            stop_at = now;
            supervisor_.request_stop();
        }
        if (stop_sent && !escalated && supervisor_.running() &&
            now - stop_at >= std::chrono::milliseconds(500)) {
            escalated = true;
            if (supervisor_.pid() > 0) {
                (void)::kill(-supervisor_.pid(), SIGKILL);
            }
        }

        if (auto exit = supervisor_.wait_for_exit(std::chrono::milliseconds(50))) {
            result.exit_code = exit->exit_code;
            result.term_signal = exit->term_signal;
            break;
        }
    }

    // Recoger los eventos que hayan quedado en el pipe después de la salida.
    for (const WorkerEvent& event : supervisor_.poll_events()) {
        process_event(event);
    }

    const auto finished = Clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        finished - started);
    result.duration = duration;
    if (result.last.elapsed.count() == 0) {
        result.last.elapsed = duration;
    }

    const double cpu_used = result.last.cpu_user_s + result.last.cpu_sys_s;
    const bool cpu_hard_limit =
        !stop_sent && config.cpu_limit.seconds.has_value() &&
        result.limits.cpu_seconds_status == LimitStatus::Applied &&
        cpu_used >= static_cast<double>(*config.cpu_limit.seconds);

    if (result.term_signal == SIGKILL && cpu_hard_limit) {
        result.outcome = RunOutcome::CpuLimitHit;
        if (result.detail.empty()) {
            result.detail = "limite duro de CPU alcanzado";
        }
    } else if (result.term_signal == SIGKILL) {
        result.outcome = stop_sent ? RunOutcome::Stopped
                                   : RunOutcome::ExternalKill;
        if (result.detail.empty()) {
            result.detail = stop_sent ? "detencion escalada a SIGKILL"
                                      : "posible OOM o kill externo";
        }
    } else if (result.term_signal == SIGXCPU) {
        result.outcome = RunOutcome::CpuLimitHit;
        if (result.detail.empty()) {
            result.detail = "limite blando de CPU";
        }
    } else if (result.term_signal != 0) {
        result.outcome = RunOutcome::ExternalKill;
        if (result.detail.empty()) {
            result.detail = "senal externa " + std::to_string(result.term_signal);
        }
    } else if (timed_out) {
        result.outcome = RunOutcome::Stopped;
        if (result.detail.empty()) {
            result.detail = "watchdog de pared";
        }
    } else if (result.exit_code != 0) {
        switch (result.exit_code) {
            case 10: result.outcome = RunOutcome::MemoryLimitHit; break;
            case 11: result.outcome = RunOutcome::CpuLimitHit; break;
            case 12: result.outcome = RunOutcome::Stopped; break;
            case 13: result.outcome = RunOutcome::Error; break;
            default: result.outcome = RunOutcome::Error; break;
        }
        if (result.detail.empty()) {
            result.detail = "worker salio con codigo " +
                            std::to_string(result.exit_code);
        }
    } else if (!got_result) {
        result.outcome = RunOutcome::Error;
        if (result.detail.empty()) {
            result.detail = "worker termino sin RESULT";
        }
    } else if (stop_sent && result.outcome == RunOutcome::Completed) {
        result.outcome = RunOutcome::Stopped;
        if (result.detail.empty()) {
            result.detail = "detenida por el usuario";
        }
    }

    return result;
}

}  // namespace memtest

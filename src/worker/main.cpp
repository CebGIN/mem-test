#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <sys/prctl.h>
#include <unistd.h>

#include "domain/Types.hpp"
#include "limits/CgroupCpuQuota.hpp"
#include "limits/CpuLimitNotifier.hpp"
#include "limits/ResourceLimiter.hpp"
#include "metrics/MetricsSourceFactory.hpp"
#include "process/WorkerProtocol.hpp"
#include "runtime/CancellationToken.hpp"
#include "workload/IWorkload.hpp"

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;
std::mutex g_emit_mutex;

extern "C" void on_stop_signal(int) {
    g_stop_requested = 1;
}

void install_stop_handlers() {
    struct sigaction action = {};
    action.sa_handler = on_stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    (void)sigaction(SIGTERM, &action, nullptr);
    (void)sigaction(SIGINT, &action, nullptr);
}

void emit(const memtest::WorkerEvent& event) {
    std::lock_guard<std::mutex> lock(g_emit_mutex);
    std::cout << memtest::encode_event(event) << '\n' << std::flush;
}

void emit_error(const std::string& code, const std::string& detail) {
    memtest::WorkerEvent event;
    event.type = memtest::WorkerEvent::Type::Error;
    event.fields["code"] = code;
    if (!detail.empty()) {
        event.fields["detail"] = detail;
    }
    emit(event);
    emit({memtest::WorkerEvent::Type::End, {}});
}

std::string status_with_value(memtest::LimitStatus status,
                              std::uint64_t value) {
    std::string text = memtest::limit_status_name(status);
    if (status == memtest::LimitStatus::Applied) {
        text += ":" + std::to_string(value);
    }
    return text;
}

void emit_limits(const memtest::LimitReport& limits) {
    memtest::WorkerEvent event;
    event.type = memtest::WorkerEvent::Type::Limits;
    event.fields["memory"] =
        status_with_value(limits.memory_status, limits.memory_applied);
    event.fields["cpu_seconds"] =
        status_with_value(limits.cpu_seconds_status,
                          static_cast<std::uint64_t>(limits.cpu_seconds_applied));
    event.fields["cpu_quota"] =
        status_with_value(limits.cpu_quota_status,
                          static_cast<std::uint64_t>(limits.cpu_quota_applied));
    emit(event);
}

void emit_ready() {
    std::uint64_t vm = 0;
    std::uint64_t rss = 0;
    auto source = memtest::make_metrics_source();
    if (source) {
        memtest::MetricsSnapshot sample;
        if (source->sample(::getpid(), sample)) {
            vm = sample.vm_size;
            rss = sample.vm_rss;
        }
    }
    memtest::WorkerEvent event;
    event.type = memtest::WorkerEvent::Type::Ready;
    event.fields["baseline_vm"] = std::to_string(vm);
    event.fields["baseline_rss"] = std::to_string(rss);
    emit(event);
}

int exit_code_for(memtest::RunOutcome outcome) {
    switch (outcome) {
        case memtest::RunOutcome::Completed: return 0;
        case memtest::RunOutcome::MemoryLimitHit: return 10;
        case memtest::RunOutcome::CpuLimitHit: return 11;
        case memtest::RunOutcome::Stopped: return 12;
        case memtest::RunOutcome::ExternalKill:
        case memtest::RunOutcome::Error: return 13;
    }
    return 13;
}

}  // namespace

int main(int argc, char** argv) {
    (void)::prctl(PR_SET_PDEATHSIG, SIGKILL);
    install_stop_handlers();

    const bool probe = argc > 1 && std::string(argv[1]) == "--probe";

    memtest::WorkerEvent hello;
    hello.type = memtest::WorkerEvent::Type::Hello;
    hello.fields["worker"] = "1";
    hello.fields["pid"] = std::to_string(::getpid());
    emit(hello);

    std::string config_line;
    if (!std::getline(std::cin, config_line)) {
        emit_error("no_config", "no se recibio configuracion por stdin");
        return 13;
    }

    memtest::ExperimentConfig config;
    std::string parse_error;
    if (!memtest::parse_config(config_line, config, &parse_error)) {
        emit_error("bad_config", parse_error);
        return 13;
    }

    std::string limit_detail;
    memtest::LimitReport limits =
        memtest::limits::ResourceLimiter().apply(config, &limit_detail);

    std::unique_ptr<memtest::limits::CgroupCpuQuota> quota;
    if (config.cpu_limit.quota_percent.has_value()) {
        quota = std::make_unique<memtest::limits::CgroupCpuQuota>();
        std::string quota_detail;
        if (!quota->apply(*config.cpu_limit.quota_percent, &quota_detail)) {
            emit_error("cpu_quota_unsupported",
                       quota_detail.empty()
                           ? "cuota cgroup no soportada; usá RLIMIT_CPU"
                           : quota_detail + "; usá RLIMIT_CPU");
            return 13;
        }
        limits.cpu_quota_status = memtest::LimitStatus::Applied;
        limits.cpu_quota_applied = *config.cpu_limit.quota_percent;
    }

    if (config.cpu_limit.seconds.has_value() &&
        limits.cpu_seconds_status == memtest::LimitStatus::Applied) {
        if (!memtest::limits::install_cpu_limit_notifier(
                *config.cpu_limit.seconds) &&
            limit_detail.empty()) {
            limit_detail = "no se pudo instalar el aviso de CPU";
        }
    }

    emit_ready();
    emit_limits(limits);

    if (probe) {
        emit({memtest::WorkerEvent::Type::End, {}});
        return 0;
    }

    memtest::CancellationToken token;
    std::atomic<bool> finished{false};
    std::thread monitor([&] {
        while (!finished.load(std::memory_order_relaxed)) {
            if (g_stop_requested != 0 ||
                memtest::limits::cpu_limit_reached()) {
                token.cancel();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    memtest::WorkloadContext context;
    context.config = &config;
    context.cancel = &token;
    context.progress = [](std::uint64_t done, std::uint64_t total) {
        memtest::WorkerEvent event;
        event.type = memtest::WorkerEvent::Type::Progress;
        event.fields["done"] = std::to_string(done);
        event.fields["total"] = std::to_string(total);
        emit(event);
    };

    memtest::WorkloadOutcome outcome;
    try {
        auto workload = memtest::make_workload(config.workload);
        if (!workload) {
            outcome.outcome = memtest::RunOutcome::Error;
            outcome.detail = "workload desconocido";
        } else {
            outcome = workload->run(context);
        }
    } catch (const std::exception& exception) {
        outcome.outcome = memtest::RunOutcome::Error;
        outcome.detail = std::string("excepcion en el workload: ") +
                         exception.what();
    } catch (...) {
        outcome.outcome = memtest::RunOutcome::Error;
        outcome.detail = "excepcion desconocida en el workload";
    }

    finished.store(true, std::memory_order_relaxed);
    monitor.join();

    if (memtest::limits::cpu_limit_reached()) {
        outcome.outcome = memtest::RunOutcome::CpuLimitHit;
        if (outcome.detail.empty()) {
            outcome.detail = "límite blando de CPU alcanzado";
        }
    } else if (g_stop_requested != 0) {
        outcome.outcome = memtest::RunOutcome::Stopped;
        if (outcome.detail.empty()) {
            outcome.detail = "detenida por SIGTERM";
        }
    }

    if (!limit_detail.empty()) {
        if (!outcome.detail.empty()) {
            outcome.detail += "; ";
        }
        outcome.detail += limit_detail;
    }

    if (quota && quota->applied()) {
        (void)quota->read_stats(limits.nr_throttled, limits.throttled_usec);
        quota->cleanup();
    }

    memtest::WorkerEvent result;
    result.type = memtest::WorkerEvent::Type::Result;
    result.fields["outcome"] =
        memtest::run_outcome_name(outcome.outcome);
    result.fields["expected"] = std::to_string(outcome.expected);
    result.fields["observed"] = std::to_string(outcome.observed);
    if (limits.cpu_quota_status == memtest::LimitStatus::Applied) {
        result.fields["nr_throttled"] = std::to_string(limits.nr_throttled);
        result.fields["throttled_usec"] = std::to_string(limits.throttled_usec);
    }
    if (!outcome.detail.empty()) {
        result.fields["detail"] = outcome.detail;
    }
    emit(result);
    emit({memtest::WorkerEvent::Type::End, {}});

    return exit_code_for(outcome.outcome);
}

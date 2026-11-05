#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "Suites.hpp"
#include "workload/ConcurrencyWorkload.hpp"
#include "workload/IWorkload.hpp"
#include "workload/SharedCounterWorkload.hpp"

#if defined(__linux__)
#include "metrics/ProcFs.hpp"
#include "workload/MemoryPressureWorkload.hpp"
#endif

namespace memtest::test {
namespace {

ExperimentConfig counter_config(SyncMode mode) {
    ExperimentConfig config;
    config.workload = WorkloadKind::SharedCounter;
    config.worker_mode = WorkerMode::NativeThreads;
    config.sync_mode = mode;
    config.threads = 4;
    config.increments_per_thread = 200000;
    config.wall_timeout_ms = 30000;
    return config;
}

void shared_counter_modes(Context& context) {
    const std::vector<SyncMode> modes = {SyncMode::None, SyncMode::Mutex,
                                         SyncMode::Atomic};
    for (SyncMode mode : modes) {
        const ExperimentConfig config = counter_config(mode);
        CancellationToken token;
        WorkloadContext workload_context{&config, &token, {}};

        SharedCounterWorkload workload;
        const WorkloadOutcome outcome = workload.run(workload_context);

        const std::uint64_t expected = 4ull * 200000ull;
        CHECK_EQ(context, outcome.outcome, RunOutcome::Completed);
        CHECK_EQ(context, outcome.expected, expected);
        if (mode == SyncMode::None) {
            // Data race real: el resultado es no determinista. El test solo
            // exige que el contador no supere los intentos.
            CHECK(context, outcome.observed <= outcome.expected);
        } else {
            CHECK_EQ(context, outcome.observed, expected);
        }
    }
}

void cancellation_stops_quickly(Context& context) {
    ExperimentConfig config;
    config.workload = WorkloadKind::Concurrency;
    config.worker_mode = WorkerMode::NativeThreads;
    config.threads = 4;
    config.work_units = 1000000000ull;
    config.wall_timeout_ms = 120000;

    CancellationToken token;
    WorkloadContext workload_context{&config, &token, {}};

    const auto started = std::chrono::steady_clock::now();
    std::thread canceller([&token] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        token.cancel();
    });

    ConcurrencyWorkload workload;
    const WorkloadOutcome outcome = workload.run(workload_context);

    canceller.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    CHECK_EQ(context, outcome.outcome, RunOutcome::Stopped);
    CHECK(context, elapsed < std::chrono::milliseconds(1000));
}

void factory_builds_all_workloads(Context& context) {
    const std::vector<WorkloadKind> kinds = {
        WorkloadKind::Concurrency, WorkloadKind::CpuLimit,
        WorkloadKind::MemoryPressure, WorkloadKind::SharedCounter};
    for (WorkloadKind kind : kinds) {
        CHECK(context, make_workload(kind) != nullptr);
    }
}

void cpu_bound_workloads_complete(Context& context) {
    const std::vector<WorkloadKind> kinds = {WorkloadKind::Concurrency,
                                             WorkloadKind::CpuLimit};
    for (WorkloadKind kind : kinds) {
        ExperimentConfig config;
        config.workload = kind;
        config.worker_mode = WorkerMode::RuntimeTasks;
        config.threads = 2;
        config.work_units = 4096;
        config.wall_timeout_ms = 5000;
        if (kind == WorkloadKind::CpuLimit) {
            config.cpu_limit.seconds = 1;
        }

        CancellationToken token;
        WorkloadContext workload_context{&config, &token, {}};

        std::unique_ptr<IWorkload> workload = make_workload(kind);
        const WorkloadOutcome outcome = workload->run(workload_context);
        CHECK_EQ(context, outcome.outcome, RunOutcome::Completed);
        CHECK(context, !outcome.detail.empty());
    }
}

void concurrency_total_work_is_fixed(Context& context) {
    ExperimentConfig config;
    config.workload = WorkloadKind::Concurrency;
    config.worker_mode = WorkerMode::NativeThreads;
    config.load_mode = LoadMode::TotalWork;
    config.threads = 4;
    config.work_units = 20000;
    config.duration_ms = 2000;
    config.wall_timeout_ms = 4000;

    CancellationToken token;
    WorkloadContext workload_context{&config, &token, {}};

    ConcurrencyWorkload workload;
    const WorkloadOutcome outcome = workload.run(workload_context);

    CHECK_EQ(context, outcome.outcome, RunOutcome::Completed);
    CHECK_EQ(context, outcome.expected, 20000ull);
    CHECK_EQ(context, outcome.observed, 20000ull);
}

void timed_workloads_respect_duration(Context& context) {
    const std::vector<WorkloadKind> cpu_kinds = {WorkloadKind::Concurrency,
                                                 WorkloadKind::CpuLimit};
    for (WorkloadKind kind : cpu_kinds) {
        ExperimentConfig config;
        config.workload = kind;
        config.worker_mode = WorkerMode::NativeThreads;
        config.threads = 2;
        config.work_units = 0;
        config.duration_ms = 150;
        config.wall_timeout_ms = 2150;
        if (kind == WorkloadKind::CpuLimit) {
            config.cpu_limit.seconds = 1;
        }

        CancellationToken token;
        WorkloadContext workload_context{&config, &token, {}};
        std::unique_ptr<IWorkload> workload = make_workload(kind);
        const auto started = std::chrono::steady_clock::now();
        const WorkloadOutcome outcome = workload->run(workload_context);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        CHECK_EQ(context, outcome.outcome, RunOutcome::Completed);
        CHECK(context, elapsed >= std::chrono::milliseconds(100));
        CHECK(context, elapsed < std::chrono::milliseconds(2000));
    }

    ExperimentConfig counter;
    counter.workload = WorkloadKind::SharedCounter;
    counter.worker_mode = WorkerMode::NativeThreads;
    counter.sync_mode = SyncMode::Atomic;
    counter.threads = 4;
    counter.increments_per_thread = 0;
    counter.duration_ms = 150;
    counter.wall_timeout_ms = 2150;

    CancellationToken counter_token;
    WorkloadContext counter_context{&counter, &counter_token, {}};
    SharedCounterWorkload counter_workload;
    const WorkloadOutcome counter_outcome = counter_workload.run(counter_context);
    CHECK_EQ(context, counter_outcome.outcome, RunOutcome::Completed);
    CHECK(context, counter_outcome.expected > 0);
    CHECK_EQ(context, counter_outcome.observed, counter_outcome.expected);

#if defined(__linux__)
    ExperimentConfig memory;
    memory.workload = WorkloadKind::MemoryPressure;
    memory.memory_bytes = 8ull * 1024ull * 1024ull;
    memory.page_stride_bytes = 4096;
    memory.duration_ms = 150;
    memory.wall_timeout_ms = 2150;

    CancellationToken memory_token;
    WorkloadContext memory_context{&memory, &memory_token, {}};
    MemoryPressureWorkload memory_workload;
    const auto memory_started = std::chrono::steady_clock::now();
    const WorkloadOutcome memory_outcome = memory_workload.run(memory_context);
    const auto memory_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - memory_started);
    CHECK_EQ(context, memory_outcome.outcome, RunOutcome::Completed);
    CHECK_EQ(context, memory_outcome.observed, memory_outcome.expected);
    CHECK(context, memory_elapsed >= std::chrono::milliseconds(100));
#endif
}

void shared_counter_with_task_pool(Context& context) {
    ExperimentConfig config;
    config.workload = WorkloadKind::SharedCounter;
    config.worker_mode = WorkerMode::RuntimeTasks;
    config.sync_mode = SyncMode::Atomic;
    config.threads = 4;
    config.increments_per_thread = 10000;
    config.wall_timeout_ms = 5000;

    CancellationToken token;
    WorkloadContext workload_context{&config, &token, {}};

    SharedCounterWorkload workload;
    const WorkloadOutcome outcome = workload.run(workload_context);
    CHECK_EQ(context, outcome.outcome, RunOutcome::Completed);
    CHECK_EQ(context, outcome.observed, 40000ull);
}

#if defined(__linux__)
void memory_pressure_touches_pages(Context& context) {
    ExperimentConfig config;
    config.workload = WorkloadKind::MemoryPressure;
    config.memory_bytes = 16ull * 1024ull * 1024ull;
    config.page_stride_bytes = 4096;
    config.wall_timeout_ms = 30000;

    CancellationToken token;
    WorkloadContext workload_context{&config, &token, {}};

    MemoryPressureWorkload workload;
    const WorkloadOutcome outcome = workload.run(workload_context);

    const std::uint64_t page = metrics::page_size_bytes();
    const std::uint64_t expected =
        16ull * 1024ull * 1024ull / page;
    CHECK_EQ(context, outcome.outcome, RunOutcome::Completed);
    CHECK_EQ(context, outcome.expected, expected);
    CHECK_EQ(context, outcome.observed, expected);
}

void memory_pressure_rounds_up(Context& context) {
    ExperimentConfig config;
    config.workload = WorkloadKind::MemoryPressure;
    config.memory_bytes = 10000;  // no es múltiplo de 4096
    config.page_stride_bytes = 4096;
    config.wall_timeout_ms = 30000;

    CancellationToken token;
    WorkloadContext workload_context{&config, &token, {}};

    MemoryPressureWorkload workload;
    const WorkloadOutcome outcome = workload.run(workload_context);

    const std::uint64_t page = metrics::page_size_bytes();
    const std::uint64_t expected =
        10000ull / page + (10000ull % page != 0 ? 1ull : 0ull);
    CHECK_EQ(context, outcome.outcome, RunOutcome::Completed);
    CHECK_EQ(context, outcome.expected, expected);
    CHECK_EQ(context, outcome.observed, expected);
}
#endif

}  // namespace

Suite workload_sync_suite() {
    return {
        {"contador compartido sin/con sincronizacion", shared_counter_modes},
        {"cancelacion en menos de 1 s", cancellation_stops_quickly},
        {"fabrica de workloads", factory_builds_all_workloads},
        {"cargas CPU-bound completan", cpu_bound_workloads_complete},
        {"concurrencia con trabajo total fijo", concurrency_total_work_is_fixed},
        {"duracion real en workloads", timed_workloads_respect_duration},
        {"contador con task pool", shared_counter_with_task_pool},
#if defined(__linux__)
        {"presion de memoria toca paginas", memory_pressure_touches_pages},
        {"presion de memoria redondea hacia arriba", memory_pressure_rounds_up},
#endif
    };
}

}  // namespace memtest::test

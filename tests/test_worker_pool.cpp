#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "Suites.hpp"
#include "runtime/NativeThreadPool.hpp"
#include "runtime/Range.hpp"
#include "runtime/TaskPool.hpp"

namespace memtest::test {
namespace {

void range_covers_total(Context& context) {
    constexpr std::uint64_t total = 1000;
    constexpr std::size_t parts = 7;

    std::uint64_t expected_begin = 0;
    for (std::size_t index = 0; index < parts; ++index) {
        const WorkRange range = split_range(total, parts, index);
        CHECK_EQ(context, range.begin, expected_begin);
        CHECK(context, range.size() <= 143);
        expected_begin = range.end;
    }
    CHECK_EQ(context, expected_begin, total);

    const WorkRange outside = split_range(total, parts, parts);
    CHECK(context, outside.empty());
}

void task_pool_runs_each_task_once(Context& context) {
    constexpr std::size_t task_count = 1000;
    std::vector<std::atomic<int>> hits(task_count);
    for (std::atomic<int>& hit : hits) {
        hit.store(0);
    }

    TaskPool pool(4);
    // El plan fija min(tareas, max(1, núcleos)); no asumir 4 núcleos.
    CHECK(context, pool.worker_count() >= 1);
    CHECK(context, pool.worker_count() <= 4);
    pool.parallel_for(task_count, [&hits](std::size_t index,
                                          const CancellationToken&) {
        hits[index].fetch_add(1);
    });

    for (const std::atomic<int>& hit : hits) {
        CHECK_EQ(context, hit.load(), 1);
    }
}

void task_pool_distinguishes_tasks_from_threads(Context& context) {
    // El pool limita los hilos nativos, no las tareas.
    TaskPool pool(1000);
    CHECK(context, pool.worker_count() >= 1);
    CHECK(context, pool.worker_count() <= 1000);

    std::atomic<int> hits{0};
    pool.parallel_for(64, [&hits](std::size_t, const CancellationToken&) {
        hits.fetch_add(1);
    });
    CHECK_EQ(context, hits.load(), 64);
}

void task_pool_reuses_workers(Context& context) {
    TaskPool pool(3);
    std::atomic<int> hits{0};
    for (int round = 0; round < 3; ++round) {
        pool.parallel_for(16, [&hits](std::size_t, const CancellationToken&) {
            hits.fetch_add(1);
        });
    }
    CHECK_EQ(context, hits.load(), 48);
}

void factory_builds_pools(Context& context) {
    std::unique_ptr<IWorkerPool> native =
        make_worker_pool(WorkerMode::NativeThreads, 4);
    CHECK(context, native != nullptr);
    CHECK_EQ(context, native->worker_count(), static_cast<std::size_t>(4));

    std::unique_ptr<IWorkerPool> tasks =
        make_worker_pool(WorkerMode::RuntimeTasks, 4);
    CHECK(context, tasks != nullptr);
    CHECK(context, tasks->worker_count() >= 1);
    CHECK(context, tasks->worker_count() <= 4);

    CHECK(context, make_worker_pool(WorkerMode::NativeThreads, 0) == nullptr);
}

void native_pool_runs_each_task_once(Context& context) {
    std::vector<std::atomic<int>> hits(64);
    for (std::atomic<int>& hit : hits) {
        hit.store(0);
    }

    NativeThreadPool pool(4);
    CHECK_EQ(context, pool.worker_count(), static_cast<std::size_t>(4));
    pool.parallel_for(hits.size(), [&hits](std::size_t index,
                                           const CancellationToken&) {
        hits[index].fetch_add(1);
    });

    for (const std::atomic<int>& hit : hits) {
        CHECK_EQ(context, hit.load(), 1);
    }
}

void exceptions_propagate_after_join(Context& context) {
    TaskPool tasks(3);
    bool task_failed = false;
    try {
        tasks.parallel_for(16, [](std::size_t index, const CancellationToken&) {
            if (index == 5) {
                throw std::runtime_error("fallo de tarea");
            }
        });
    } catch (const std::runtime_error&) {
        task_failed = true;
    }
    CHECK(context, task_failed);

    NativeThreadPool native(3);
    bool native_failed = false;
    try {
        native.parallel_for(16, [](std::size_t index,
                                   const CancellationToken&) {
            if (index == 5) {
                throw std::runtime_error("fallo de tarea");
            }
        });
    } catch (const std::runtime_error&) {
        native_failed = true;
    }
    CHECK(context, native_failed);
}

void cancelled_pool_does_not_start_tasks(Context& context) {
    CancellationToken token;
    token.cancel();

    std::atomic<int> hits{0};
    TaskPool tasks(4, &token);
    tasks.parallel_for(1000, [&hits](std::size_t, const CancellationToken&) {
        hits.fetch_add(1);
    });
    CHECK_EQ(context, hits.load(), 0);

    NativeThreadPool native(4, &token);
    native.parallel_for(1000, [&hits](std::size_t, const CancellationToken&) {
        hits.fetch_add(1);
    });
    CHECK_EQ(context, hits.load(), 0);
}

}  // namespace

Suite worker_pool_suite() {
    return {
        {"rangos sin solapamiento", range_covers_total},
        {"task pool ejecuta 1000 tareas", task_pool_runs_each_task_once},
        {"task pool distingue tareas de hilos",
         task_pool_distinguishes_tasks_from_threads},
        {"task pool reutiliza sus hilos", task_pool_reuses_workers},
        {"fabrica de pools", factory_builds_pools},
        {"native pool ejecuta cada tarea", native_pool_runs_each_task_once},
        {"excepciones se propagan tras unir", exceptions_propagate_after_join},
        {"cancelado no arranca tareas", cancelled_pool_does_not_start_tasks},
    };
}

}  // namespace memtest::test

#include "workload/SharedCounterWorkload.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "runtime/Range.hpp"
#include "workload/WorkloadSupport.hpp"

namespace memtest {
namespace {

constexpr std::uint64_t kCheckpointUnits = 256;
constexpr std::uint64_t kYieldEvery = 1024;

// Contador compartido con los tres modos de sincronizacion. `None` usa un
// contador no atomico a proposito: hay data race real (comportamiento
// indefinido en C++) y las actualizaciones perdidas no son deterministas. Es
// una demostracion docente, no un patron de produccion. `Mutex` y `Atomic` son
// correctos.
class SharedCounter {
public:
    void increment(SyncMode mode) {
        switch (mode) {
            case SyncMode::None: {
                const std::uint64_t current = racy_value_;
                racy_value_ = current + 1;
                break;
            }
            case SyncMode::Mutex: {
                std::lock_guard<std::mutex> lock(mutex_);
                ++value_;
                break;
            }
            case SyncMode::Atomic:
                atomic_value_.fetch_add(1, std::memory_order_relaxed);
                break;
        }
    }

    std::uint64_t value(SyncMode mode) const {
        switch (mode) {
            case SyncMode::None:
                return racy_value_;
            case SyncMode::Mutex:
                return value_;
            case SyncMode::Atomic:
                return atomic_value_.load(std::memory_order_relaxed);
        }
        return 0;
    }

private:
    // `volatile` evita que el compilador colapse el load/store, pero no hace
    // atomica la operacion: el data race sigue siendo deliberado.
    volatile std::uint64_t racy_value_{0};
    std::uint64_t value_{0};
    std::atomic<std::uint64_t> atomic_value_{0};
    std::mutex mutex_;
};

}  // namespace

WorkloadOutcome SharedCounterWorkload::run(const WorkloadContext& context) {
    WorkloadOutcome result;
    if (context.config == nullptr || context.config->threads < 2 ||
        (context.config->duration_ms == 0 &&
         context.config->increments_per_thread < 1)) {
        result.outcome = RunOutcome::Error;
        result.detail =
            "configuracion invalida: hilos >= 2 y, sin duración, incrementos >= 1";
        return result;
    }

    const ExperimentConfig& config = *context.config;
    const int tasks = config.threads;
    const SyncMode mode = config.sync_mode;
    const bool timed =
        config.duration_ms > 0 && config.increments_per_thread == 0;
    const std::uint64_t expected =
        timed ? 0
              : saturating_mul(static_cast<std::uint64_t>(tasks),
                               config.increments_per_thread);
    const auto deadline = timed ? workload_deadline(config)
                                : std::chrono::steady_clock::time_point{};
    result.expected = expected;

    std::unique_ptr<IWorkerPool> pool = make_pool_for_workload(context, tasks);
    if (!pool) {
        result.outcome = RunOutcome::Error;
        result.detail = "no se pudo crear el pool de trabajo";
        return result;
    }

    SharedCounter counter;
    std::atomic<std::uint64_t> attempts{0};
    std::atomic<bool> cancelled{false};
    const auto started = std::chrono::steady_clock::now();
    ProgressReporter progress(context.progress, expected);

    pool->parallel_for(static_cast<std::size_t>(tasks),
                       [&](std::size_t index, const CancellationToken& token) {
                           std::uint64_t completed = 0;

                           auto increment_chunk = [&](std::uint64_t units) {
                               for (std::uint64_t unit = 0; unit < units; ++unit) {
                                   counter.increment(mode);
                                   ++completed;
                                   // El mismo yield en los tres modos evita
                                   // sesgar la comparacion de throughput.
                                   if (unit % kYieldEvery == 0) {
                                       std::this_thread::yield();
                                   }
                               }
                           };

                           if (timed) {
                               while (!workload_time_expired(deadline)) {
                                   if (workload_cancelled(context, token)) {
                                       cancelled.store(true,
                                                       std::memory_order_relaxed);
                                       break;
                                   }
                                   increment_chunk(kCheckpointUnits);
                               }
                           } else {
                               // Cada tarea recibe un tramo contiguo del total;
                               // la union cubre exactamente lo esperado.
                               const WorkRange range = split_range(
                                   expected, static_cast<std::size_t>(tasks), index);
                               std::uint64_t unit = 0;
                               while (unit < range.size()) {
                                   if (workload_cancelled(context, token)) {
                                       cancelled.store(true,
                                                       std::memory_order_relaxed);
                                       break;
                                   }
                                   const std::uint64_t chunk =
                                       std::min(kCheckpointUnits,
                                                range.size() - unit);
                                   increment_chunk(chunk);
                                   unit += chunk;
                               }
                           }

                           if (completed > 0) {
                               attempts.fetch_add(completed,
                                                  std::memory_order_relaxed);
                               progress.add(completed);
                           }
                       });

    const std::uint64_t observed = counter.value(mode);
    const std::uint64_t actual_expected =
        timed ? attempts.load(std::memory_order_relaxed) : expected;
    const std::uint64_t lost =
        actual_expected > observed ? actual_expected - observed : 0;
    const auto finished = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(finished - started).count();

    result.expected = actual_expected;
    result.observed = observed;
    result.outcome = cancelled.load(std::memory_order_relaxed)
                         ? RunOutcome::Stopped
                         : RunOutcome::Completed;
    result.detail = "tareas=" + std::to_string(tasks) +
                    " hilos_nativos=" + std::to_string(pool->worker_count()) +
                    " esperado=" + std::to_string(actual_expected) +
                    " observado=" + std::to_string(observed) +
                    " perdidas=" + std::to_string(lost);
    if (timed) {
        result.detail += " ops/s=" +
                         std::to_string(seconds > 0.0
                                            ? static_cast<std::uint64_t>(
                                                  static_cast<double>(actual_expected) /
                                                  seconds)
                                            : 0);
    }
    return result;
}

}  // namespace memtest

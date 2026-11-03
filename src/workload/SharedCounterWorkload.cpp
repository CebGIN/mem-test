#include "workload/SharedCounterWorkload.hpp"

#include <atomic>
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

// Contador compartido con los tres modos de sincronizacion. En `None` las
// operaciones son atomicas relajadas, asi que no hay data race en el modelo de
// C++, pero el load-modify-store si pierde actualizaciones.
class SharedCounter {
public:
    void increment(SyncMode mode) {
        switch (mode) {
            case SyncMode::None: {
                const std::uint64_t current =
                    value_.load(std::memory_order_relaxed);
                value_.store(current + 1, std::memory_order_relaxed);
                break;
            }
            case SyncMode::Mutex: {
                std::lock_guard<std::mutex> lock(mutex_);
                const std::uint64_t current =
                    value_.load(std::memory_order_relaxed);
                value_.store(current + 1, std::memory_order_relaxed);
                break;
            }
            case SyncMode::Atomic:
                value_.fetch_add(1, std::memory_order_relaxed);
                break;
        }
    }

    std::uint64_t value() const {
        return value_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> value_{0};
    std::mutex mutex_;
};

}  // namespace

WorkloadOutcome SharedCounterWorkload::run(const WorkloadContext& context) {
    WorkloadOutcome result;
    if (context.config == nullptr || context.config->threads < 1 ||
        context.config->increments_per_thread < 1) {
        result.outcome = RunOutcome::Error;
        result.detail = "configuracion invalida: hilos e incrementos deben ser >= 1";
        return result;
    }

    const ExperimentConfig& config = *context.config;
    const int tasks = config.threads;
    const SyncMode mode = config.sync_mode;
    const std::uint64_t expected =
        saturating_mul(static_cast<std::uint64_t>(tasks),
                       config.increments_per_thread);
    result.expected = expected;

    std::unique_ptr<IWorkerPool> pool = make_pool_for_workload(context, tasks);
    if (!pool) {
        result.outcome = RunOutcome::Error;
        result.detail = "no se pudo crear el pool de trabajo";
        return result;
    }

    SharedCounter counter;
    std::atomic<bool> cancelled{false};
    ProgressReporter progress(context.progress, expected);

    pool->parallel_for(static_cast<std::size_t>(tasks),
                       [&](std::size_t index, const CancellationToken& token) {
                           // Cada tarea recibe un tramo contiguo del total; la
                           // union de tramos cubre exactamente lo esperado.
                           const WorkRange range =
                               split_range(expected, static_cast<std::size_t>(tasks),
                                           index);
                           std::uint64_t completed = 0;
                           for (std::uint64_t unit = 0; unit < range.size();
                                ++unit) {
                               if (unit % kCheckpointUnits == 0 &&
                                   workload_cancelled(context, token)) {
                                   cancelled.store(true, std::memory_order_relaxed);
                                   break;
                               }

                               counter.increment(mode);
                               ++completed;

                               if (mode == SyncMode::None &&
                                   unit % kYieldEvery == 0) {
                                   std::this_thread::yield();
                               }
                           }

                           if (completed > 0) {
                               progress.add(completed);
                           }
                       });

    const std::uint64_t observed = counter.value();
    const std::uint64_t lost = expected > observed ? expected - observed : 0;

    result.observed = observed;
    result.outcome = cancelled.load(std::memory_order_relaxed)
                         ? RunOutcome::Stopped
                         : RunOutcome::Completed;
    result.detail = "tareas=" + std::to_string(tasks) +
                    " hilos_nativos=" + std::to_string(pool->worker_count()) +
                    " esperado=" + std::to_string(expected) +
                    " observado=" + std::to_string(observed) +
                    " perdidas=" + std::to_string(lost);
    return result;
}

}  // namespace memtest

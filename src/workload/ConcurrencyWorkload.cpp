#include "workload/ConcurrencyWorkload.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "workload/WorkloadSupport.hpp"

namespace memtest {
namespace {

// Checkpoint de cancelacion y progreso, en unidades de trabajo
// (Arquitectura.md §11.2).
constexpr std::uint64_t kCheckpointUnits = 256;

// Hash entero barato y reproducible; no depende de la plataforma.
inline std::uint64_t mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return value;
}

}  // namespace

WorkloadOutcome ConcurrencyWorkload::run(const WorkloadContext& context) {
    WorkloadOutcome result;
    if (context.config == nullptr || context.config->threads < 1) {
        result.outcome = RunOutcome::Error;
        result.detail = "configuracion invalida: se requiere al menos un hilo";
        return result;
    }

    const ExperimentConfig& config = *context.config;
    const int tasks = config.threads;
    const std::uint64_t total = saturating_mul(static_cast<std::uint64_t>(tasks),
                                               config.work_units);
    ProgressReporter progress(context.progress, total);

    std::unique_ptr<IWorkerPool> pool = make_pool_for_workload(context, tasks);
    if (!pool) {
        result.outcome = RunOutcome::Error;
        result.detail = "no se pudo crear el pool de trabajo";
        return result;
    }

    std::vector<std::uint64_t> checksums(static_cast<std::size_t>(tasks), 0);
    std::atomic<std::uint64_t> done{0};
    std::atomic<bool> cancelled{false};

    pool->parallel_for(static_cast<std::size_t>(tasks),
                       [&](std::size_t index, const CancellationToken& token) {
                           std::uint64_t state =
                               mix(0x9e3779b97f4a7c15ull + index);
                           std::uint64_t checksum = 0;
                           std::uint64_t completed = 0;

                           for (std::uint64_t unit = 0; unit < config.work_units;
                                ++unit) {
                               if (unit % kCheckpointUnits == 0 &&
                                   workload_cancelled(context, token)) {
                                   cancelled.store(true, std::memory_order_relaxed);
                                   break;
                               }
                               state = mix(state + unit);
                               checksum += state;
                               ++completed;
                           }

                           checksums[index] = checksum;
                           if (completed > 0) {
                               done.fetch_add(completed, std::memory_order_relaxed);
                               progress.add(completed);
                           }
                       });

    std::uint64_t combined = 0;
    for (std::uint64_t checksum : checksums) {
        combined ^= checksum;
    }

    result.outcome = cancelled.load(std::memory_order_relaxed)
                         ? RunOutcome::Stopped
                         : RunOutcome::Completed;
    result.detail = "tareas=" + std::to_string(tasks) +
                    " hilos_nativos=" + std::to_string(pool->worker_count()) +
                    " unidades=" + std::to_string(done.load(std::memory_order_relaxed)) +
                    " checksum=" + std::to_string(combined);
    return result;
}

}  // namespace memtest

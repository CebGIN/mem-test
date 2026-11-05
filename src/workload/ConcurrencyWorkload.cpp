#include "workload/ConcurrencyWorkload.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/Range.hpp"
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
    const bool fixed_total = config.load_mode == LoadMode::TotalWork;
    const bool timed = !fixed_total && workload_uses_duration(config);
    // En trabajo total la duracion no define la carga: solo actua como techo
    // de seguridad para que una configuracion grande no bloquee la TUI.
    const bool capped = fixed_total && config.duration_ms > 0;
    const auto deadline = (timed || capped)
                              ? workload_deadline(config)
                              : std::chrono::steady_clock::time_point{};

    if (fixed_total && config.work_units < 1) {
        result.outcome = RunOutcome::Error;
        result.detail = "configuracion invalida: el trabajo total debe ser >= 1";
        return result;
    }

    const std::uint64_t total =
        fixed_total ? config.work_units
                    : (timed ? 0
                             : saturating_mul(static_cast<std::uint64_t>(tasks),
                                              config.work_units));
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
    std::atomic<bool> deadline_reached{false};
    const auto started = std::chrono::steady_clock::now();

    pool->parallel_for(static_cast<std::size_t>(tasks),
                       [&](std::size_t index, const CancellationToken& token) {
                           std::uint64_t state =
                               mix(0x9e3779b97f4a7c15ull + index);
                           std::uint64_t checksum = 0;
                           std::uint64_t completed = 0;

                           auto do_chunk = [&](std::uint64_t units) {
                               for (std::uint64_t unit = 0; unit < units; ++unit) {
                                   state = mix(state + unit);
                                   checksum += state;
                                   ++completed;
                               }
                           };

                           if (fixed_total) {
                               const WorkRange range = split_range(
                                   total, static_cast<std::size_t>(tasks), index);
                               std::uint64_t unit = 0;
                               while (unit < range.size()) {
                                   if (workload_cancelled(context, token)) {
                                       cancelled.store(true,
                                                       std::memory_order_relaxed);
                                       break;
                                   }
                                   if (capped && workload_time_expired(deadline)) {
                                       deadline_reached.store(
                                           true, std::memory_order_relaxed);
                                       break;
                                   }
                                   const std::uint64_t chunk =
                                       std::min(kCheckpointUnits,
                                                range.size() - unit);
                                   do_chunk(chunk);
                                   unit += chunk;
                               }
                           } else if (timed) {
                               while (!workload_time_expired(deadline)) {
                                   if (workload_cancelled(context, token)) {
                                       cancelled.store(true,
                                                       std::memory_order_relaxed);
                                       break;
                                   }
                                   do_chunk(kCheckpointUnits);
                               }
                           } else {
                               std::uint64_t unit = 0;
                               while (unit < config.work_units) {
                                   if (workload_cancelled(context, token)) {
                                       cancelled.store(true,
                                                       std::memory_order_relaxed);
                                       break;
                                   }
                                   const std::uint64_t remaining =
                                       config.work_units - unit;
                                   const std::uint64_t chunk =
                                       std::min(kCheckpointUnits, remaining);
                                   do_chunk(chunk);
                                   unit += chunk;
                               }
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

    const std::uint64_t completed = done.load(std::memory_order_relaxed);
    const auto finished = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(finished - started).count();

    const bool stopped = cancelled.load(std::memory_order_relaxed) ||
                         deadline_reached.load(std::memory_order_relaxed);
    result.outcome = stopped ? RunOutcome::Stopped : RunOutcome::Completed;
    result.expected = (fixed_total || !timed) ? total : completed;
    result.observed = completed;
    result.detail = "tareas=" + std::to_string(tasks) +
                    " hilos_nativos=" + std::to_string(pool->worker_count()) +
                    " esperado=" + std::to_string(result.expected) +
                    " unidades=" + std::to_string(completed) +
                    " ops/s=" +
                    std::to_string(seconds > 0.0
                                       ? static_cast<std::uint64_t>(
                                             static_cast<double>(completed) / seconds)
                                       : 0) +
                    " checksum=" + std::to_string(combined);
    if (deadline_reached.load(std::memory_order_relaxed)) {
        result.detail += " techo_duracion=alcanzado";
    }
    return result;
}

}  // namespace memtest

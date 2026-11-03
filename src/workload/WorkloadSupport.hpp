#ifndef MEMTEST_WORKLOAD_WORKLOADSUPPORT_HPP
#define MEMTEST_WORKLOAD_WORKLOADSUPPORT_HPP

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#include "runtime/NativeThreadPool.hpp"
#include "runtime/TaskPool.hpp"
#include "workload/IWorkload.hpp"

namespace memtest {

// Multiplicacion sin desbordar: los totales esperados se saturan en el maximo
// de uint64_t para no envolverse con configuraciones grandes.
inline std::uint64_t saturating_mul(std::uint64_t lhs, std::uint64_t rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    if (lhs > maximum / rhs) {
        return maximum;
    }
    return lhs * rhs;
}

// El worker es dueno del CancellationToken y lo muta desde su hilo principal;
// WorkloadContext lo expone como const porque el workload solo lo observa. El
// pool recibe el mismo token para dejar de programar tareas al cancelarse.
inline std::unique_ptr<IWorkerPool> make_pool_for_workload(
    const WorkloadContext& context, int tasks) {
    CancellationToken* shared = const_cast<CancellationToken*>(context.cancel);
    if (context.config != nullptr &&
        context.config->worker_mode == WorkerMode::RuntimeTasks) {
        return std::make_unique<TaskPool>(tasks, shared);
    }
    return std::make_unique<NativeThreadPool>(tasks, shared);
}

inline bool workload_cancelled(const WorkloadContext& context,
                               const CancellationToken& token) {
    return token.cancelled() ||
           (context.cancel != nullptr && context.cancel->cancelled());
}

// Reporte de progreso seguro entre tareas. El callback puede ser costoso; los
// workloads lo invocan solo en sus checkpoints, no en cada unidad.
class ProgressReporter {
public:
    ProgressReporter(std::function<void(std::uint64_t, std::uint64_t)> callback,
                     std::uint64_t total)
        : callback_(std::move(callback)), total_(total) {}

    void add(std::uint64_t delta) {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ += delta;
        emit_locked();
    }

    void set(std::uint64_t done) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (done > done_) {
            done_ = done;
        }
        emit_locked();
    }

private:
    void emit_locked() {
        if (callback_) {
            callback_(done_, total_);
        }
    }

    std::mutex mutex_;
    std::function<void(std::uint64_t, std::uint64_t)> callback_;
    std::uint64_t total_ = 0;
    std::uint64_t done_ = 0;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_WORKLOADSUPPORT_HPP

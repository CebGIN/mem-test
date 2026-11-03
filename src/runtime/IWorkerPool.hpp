#ifndef MEMTEST_RUNTIME_IWORKERPOOL_HPP
#define MEMTEST_RUNTIME_IWORKERPOOL_HPP

#include <cstddef>
#include <functional>
#include <memory>

#include "domain/Types.hpp"
#include "runtime/CancellationToken.hpp"

namespace memtest {

// Reparte `task_count` tareas sobre el runtime elegido: un hilo nativo por
// tarea (NativeThreads) o un pool fijo con cola FIFO (RuntimeTasks)
// (Arquitectura.md §11 y §12.1).
class IWorkerPool {
public:
    virtual ~IWorkerPool() = default;
    virtual void parallel_for(
        std::size_t task_count,
        const std::function<void(std::size_t, const CancellationToken&)>& body) = 0;
    virtual std::size_t worker_count() const = 0;
};

// Devuelve el pool correspondiente al modo. `tasks` es la cantidad de tareas
// que verá el workload, no necesariamente la de hilos nativos.
std::unique_ptr<IWorkerPool> make_worker_pool(WorkerMode mode, int tasks);

}  // namespace memtest

#endif  // MEMTEST_RUNTIME_IWORKERPOOL_HPP

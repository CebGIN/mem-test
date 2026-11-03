#include "runtime/IWorkerPool.hpp"

#include "runtime/NativeThreadPool.hpp"
#include "runtime/TaskPool.hpp"

namespace memtest {

std::unique_ptr<IWorkerPool> make_worker_pool(WorkerMode mode, int tasks) {
    if (tasks <= 0) {
        // Un pool vacio no es un experimento valido; la validacion lo rechaza
        // antes de llegar aqui (Arquitectura.md §10).
        return nullptr;
    }

    switch (mode) {
        case WorkerMode::NativeThreads:
            return std::make_unique<NativeThreadPool>(tasks);
        case WorkerMode::RuntimeTasks:
            return std::make_unique<TaskPool>(tasks);
    }
    return nullptr;
}

}  // namespace memtest

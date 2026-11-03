#ifndef MEMTEST_RUNTIME_TASKPOOL_HPP
#define MEMTEST_RUNTIME_TASKPOOL_HPP

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "runtime/CancellationToken.hpp"
#include "runtime/IWorkerPool.hpp"

namespace memtest {

// Pool fijo de hilos nativos con cola FIFO. `task_count` tareas se reparten
// sobre min(task_count, max(1, nucleos)) hilos (Arquitectura.md §12.1,
// WorkerMode::RuntimeTasks). La distincion tarea/hilo se observa con
// worker_count().
class TaskPool final : public IWorkerPool {
public:
    // `shared_token` permite observar el token que ya posee el worker. Si es
    // nulo, el pool crea y posee su propio token.
    explicit TaskPool(int tasks, CancellationToken* shared_token = nullptr);
    ~TaskPool() override;

    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    void parallel_for(
        std::size_t task_count,
        const std::function<void(std::size_t, const CancellationToken&)>& body) override;

    // Cantidad de hilos nativos del pool, no de tareas.
    std::size_t worker_count() const override;

    // Cancela el token del pool (si es propio) y deja de encolar tareas nuevas.
    void cancel();

    const CancellationToken& token() const;

private:
    void worker_loop();

    std::size_t worker_count_;
    CancellationToken owned_token_;
    CancellationToken* token_;

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable done_cv_;
    std::deque<std::size_t> queue_;
    const std::function<void(std::size_t, const CancellationToken&)>* body_ = nullptr;
    bool active_ = false;
    bool stop_ = false;
    std::size_t running_ = 0;
    std::exception_ptr error_;
};

}  // namespace memtest

#endif  // MEMTEST_RUNTIME_TASKPOOL_HPP

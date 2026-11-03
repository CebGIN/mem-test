#ifndef MEMTEST_RUNTIME_NATIVETHREADPOOL_HPP
#define MEMTEST_RUNTIME_NATIVETHREADPOOL_HPP

#include <cstddef>
#include <functional>

#include "runtime/CancellationToken.hpp"
#include "runtime/IWorkerPool.hpp"

namespace memtest {

// Pool 1:1: cada tarea se ejecuta en su propio hilo nativo
// (Arquitectura.md §12.1, WorkerMode::NativeThreads).
class NativeThreadPool final : public IWorkerPool {
public:
    // `shared_token` permite observar el token que ya posee el worker (por
    // ejemplo el que cancelan SIGTERM/SIGXCPU). Si es nulo, el pool crea y
    // posee su propio token.
    explicit NativeThreadPool(int tasks, CancellationToken* shared_token = nullptr);
    ~NativeThreadPool() override = default;

    NativeThreadPool(const NativeThreadPool&) = delete;
    NativeThreadPool& operator=(const NativeThreadPool&) = delete;

    void parallel_for(
        std::size_t task_count,
        const std::function<void(std::size_t, const CancellationToken&)>& body) override;

    // Cantidad de hilos nativos que puede ejecutar el pool.
    std::size_t worker_count() const override;

    // Cancela el token del pool (si es propio) y despierta a las tareas.
    void cancel();

    // Token que recibe cada tarea en `parallel_for`.
    const CancellationToken& token() const;

private:
    std::size_t tasks_;
    CancellationToken owned_token_;
    CancellationToken* token_;
};

}  // namespace memtest

#endif  // MEMTEST_RUNTIME_NATIVETHREADPOOL_HPP

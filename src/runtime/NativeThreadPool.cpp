#include "runtime/NativeThreadPool.hpp"

#include <exception>
#include <thread>
#include <utility>
#include <vector>

namespace memtest {

NativeThreadPool::NativeThreadPool(int tasks, CancellationToken* shared_token)
    : tasks_(tasks > 0 ? static_cast<std::size_t>(tasks) : 0),
      token_(shared_token != nullptr ? shared_token : &owned_token_) {}

void NativeThreadPool::parallel_for(
    std::size_t task_count,
    const std::function<void(std::size_t, const CancellationToken&)>& body) {
    if (task_count == 0) {
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(task_count);
    std::vector<std::exception_ptr> errors(task_count);

    // 1:1: un hilo nativo por tarea. Si el token ya esta cancelado no se
    // programan tareas nuevas; las que alcanzaron a arrancar se esperan.
    std::exception_ptr spawn_error;
    for (std::size_t index = 0; index < task_count; ++index) {
        if (token_->cancelled()) {
            break;
        }
        try {
            threads.emplace_back([&body, &errors, this, index] {
                try {
                    body(index, *token_);
                } catch (...) {
                    errors[index] = std::current_exception();
                }
            });
        } catch (...) {
            spawn_error = std::current_exception();
            break;
        }
    }

    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    if (spawn_error) {
        std::rethrow_exception(spawn_error);
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

std::size_t NativeThreadPool::worker_count() const { return tasks_; }

void NativeThreadPool::cancel() { token_->cancel(); }

const CancellationToken& NativeThreadPool::token() const { return *token_; }

}  // namespace memtest

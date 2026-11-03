#include "runtime/TaskPool.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace memtest {
namespace {

std::size_t native_cores() {
    const unsigned int detected = std::thread::hardware_concurrency();
    return detected > 0 ? static_cast<std::size_t>(detected) : 1u;
}

}  // namespace

TaskPool::TaskPool(int tasks, CancellationToken* shared_token)
    : worker_count_(tasks > 0
                        ? std::min(static_cast<std::size_t>(tasks), native_cores())
                        : 0),
      token_(shared_token != nullptr ? shared_token : &owned_token_) {
    threads_.reserve(worker_count_);
    try {
        for (std::size_t index = 0; index < worker_count_; ++index) {
            threads_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        work_cv_.notify_all();
        for (std::thread& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        throw;
    }
}

TaskPool::~TaskPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    work_cv_.notify_all();
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void TaskPool::parallel_for(
    std::size_t task_count,
    const std::function<void(std::size_t, const CancellationToken&)>& body) {
    if (worker_count_ == 0 || task_count == 0) {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (active_) {
            throw std::logic_error("TaskPool::parallel_for no es reentrante");
        }
        active_ = true;
        error_ = nullptr;
        body_ = &body;
        queue_.clear();
        for (std::size_t index = 0; index < task_count; ++index) {
            queue_.push_back(index);
        }
    }
    work_cv_.notify_all();

    std::exception_ptr error;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // Se espera a que no quede nada en cola y a que terminen las tareas ya
        // empezadas. Si hay cancelacion, las que siguen en cola se descartan.
        done_cv_.wait(lock, [this] {
            return running_ == 0 && (queue_.empty() || token_->cancelled());
        });
        active_ = false;
        body_ = nullptr;
        queue_.clear();
        error = error_;
        error_ = nullptr;
    }
    work_cv_.notify_all();

    if (error) {
        std::rethrow_exception(error);
    }
}

std::size_t TaskPool::worker_count() const { return worker_count_; }

void TaskPool::cancel() {
    {
        // El token se toca bajo el mutex para que un worker que esta a punto de
        // dormir no pierda el aviso de cancelacion.
        std::lock_guard<std::mutex> lock(mutex_);
        token_->cancel();
    }
    work_cv_.notify_all();
}

const CancellationToken& TaskPool::token() const { return *token_; }

void TaskPool::worker_loop() {
    for (;;) {
        std::size_t index = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_cv_.wait(lock, [this] {
                return stop_ ||
                       (active_ && (token_->cancelled() || !queue_.empty()));
            });

            if (stop_) {
                return;
            }
            if (!active_) {
                continue;
            }
            if (token_->cancelled()) {
                // La cancelacion impide programar tareas nuevas; las que ya
                // empezaron siguen hasta su proximo checkpoint.
                queue_.clear();
                done_cv_.notify_all();
                continue;
            }
            if (queue_.empty()) {
                continue;
            }

            index = queue_.front();
            queue_.pop_front();
            ++running_;
        }

        try {
            (*body_)(index, *token_);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) {
                error_ = std::current_exception();
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --running_;
        }
        done_cv_.notify_all();
    }
}

}  // namespace memtest

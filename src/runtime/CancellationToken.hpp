#ifndef MEMTEST_RUNTIME_CANCELLATIONTOKEN_HPP
#define MEMTEST_RUNTIME_CANCELLATIONTOKEN_HPP

#include <atomic>

namespace memtest {

// Bandera cooperativa que comparten los workloads y el runtime. El handler de
// señal no la toca: escribe g_signal_flag y el worker llama a cancel()
// (Arquitectura.md §11.2).
class CancellationToken {
public:
    void cancel() { flag_.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return flag_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> flag_{false};
};

}  // namespace memtest

#endif  // MEMTEST_RUNTIME_CANCELLATIONTOKEN_HPP

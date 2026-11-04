#include "limits/CpuLimitNotifier.hpp"

#include <csignal>

namespace memtest::limits {

namespace {

volatile std::sig_atomic_t g_cpu_limit_flag = 0;

extern "C" void on_sigxcpu(int) {
    g_cpu_limit_flag = 1;  // lo único seguro dentro de un handler
}

}  // namespace

bool install_cpu_limit_notifier(int /*soft_seconds*/) {
    struct sigaction action = {};
    action.sa_handler = on_sigxcpu;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    return sigaction(SIGXCPU, &action, nullptr) == 0;
}

bool cpu_limit_reached() noexcept {
    return g_cpu_limit_flag != 0;
}

}  // namespace memtest::limits

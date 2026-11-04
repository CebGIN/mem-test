#include "limits/CpuLimitNotifier.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

namespace memtest::limits {

namespace {

std::atomic<bool> g_cpu_limit_flag{false};
std::atomic<bool> g_started{false};
std::atomic<int> g_soft_seconds{0};

double filetime_to_seconds(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return static_cast<double>(v.QuadPart) / 1e7;
}

DWORD WINAPI watch_cpu_time(LPVOID) {
    const HANDLE self = GetCurrentProcess();
    while (!g_cpu_limit_flag.load(std::memory_order_relaxed)) {
        FILETIME creation, exit_time, kernel, user;
        if (GetProcessTimes(self, &creation, &exit_time, &kernel, &user)) {
            const double used =
                filetime_to_seconds(kernel) + filetime_to_seconds(user);
            if (used >= static_cast<double>(g_soft_seconds.load())) {
                g_cpu_limit_flag.store(true, std::memory_order_relaxed);
                break;
            }
        }
        Sleep(10);
    }
    return 0;
}

}  // namespace

bool install_cpu_limit_notifier(int soft_seconds) {
    if (soft_seconds < 1) {
        return false;
    }
    g_soft_seconds.store(soft_seconds);
    if (g_started.exchange(true)) {
        return true;  // el vigía ya corre; solo se actualizó el umbral
    }
    HANDLE thread = CreateThread(nullptr, 0, watch_cpu_time, nullptr, 0, nullptr);
    if (thread == nullptr) {
        g_started.store(false);
        return false;
    }
    CloseHandle(thread);  // el hilo sigue; no hace falta el handle
    return true;
}

bool cpu_limit_reached() noexcept {
    return g_cpu_limit_flag.load(std::memory_order_relaxed);
}

}  // namespace memtest::limits

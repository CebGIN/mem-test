#include "metrics/WinMetricsSource.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>

namespace memtest::metrics {

namespace {

// 1 FILETIME = 100 ns.
double filetime_to_seconds(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return static_cast<double>(v.QuadPart) / 1e7;
}

class HandleGuard {
public:
    explicit HandleGuard(HANDLE h) : handle_(h) {}
    ~HandleGuard() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
    HANDLE get() const { return handle_; }

private:
    HANDLE handle_;
};

int count_threads(DWORD pid) {
    HandleGuard snap(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (snap.get() == INVALID_HANDLE_VALUE) {
        return 0;
    }
    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    int count = 0;
    if (Thread32First(snap.get(), &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                ++count;
            }
        } while (Thread32Next(snap.get(), &entry));
    }
    return count;
}

bool read_global_memory(MEMORYSTATUSEX& status) {
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) != 0;
}

// Archivo de paginación aproximado: TotalPageFile es el límite de commit
// (RAM + paginación), así que la paginación es lo que excede a la RAM.
std::uint64_t approx_swap_total(const MEMORYSTATUSEX& s) {
    return s.ullTotalPageFile > s.ullTotalPhys
               ? s.ullTotalPageFile - s.ullTotalPhys
               : 0;
}

std::uint64_t approx_swap_free(const MEMORYSTATUSEX& s) {
    const std::uint64_t total = approx_swap_total(s);
    const std::uint64_t free_commit =
        s.ullAvailPageFile > s.ullAvailPhys ? s.ullAvailPageFile - s.ullAvailPhys
                                            : 0;
    return std::min(total, free_commit);
}

}  // namespace

bool WinMetricsSource::sample(pid_t pid, MetricsSnapshot& out) const {
    HandleGuard process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                    static_cast<DWORD>(pid)));
    if (process.get() == nullptr) {
        return false;
    }

    PROCESS_MEMORY_COUNTERS_EX mem{};
    mem.cb = sizeof(mem);
    if (!GetProcessMemoryInfo(
            process.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),
            sizeof(mem))) {
        return false;
    }

    FILETIME creation, exit_time, kernel, user;
    if (!GetProcessTimes(process.get(), &creation, &exit_time, &kernel, &user)) {
        return false;
    }
    // Un proceso terminado sigue siendo abrible mientras alguien conserve un
    // handle; si ya salió, no hay nada que muestrear.
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process.get(), &exit_code) &&
        exit_code != STILL_ACTIVE) {
        return false;
    }

    MetricsSnapshot snap;
    snap.elapsed = out.elapsed;  // lo gestiona quien llama
    snap.vm_rss = mem.WorkingSetSize;
    snap.vm_hwm = mem.PeakWorkingSetSize;
    snap.vm_size = std::max<std::uint64_t>(mem.PrivateUsage, mem.WorkingSetSize);
    snap.minor_faults = mem.PageFaultCount;
    snap.cpu_user_s = filetime_to_seconds(user);
    snap.cpu_sys_s = filetime_to_seconds(kernel);
    snap.threads = count_threads(static_cast<DWORD>(pid));

    MEMORYSTATUSEX global{};
    if (read_global_memory(global)) {
        snap.sys_swap_total = approx_swap_total(global);
        snap.sys_swap_free = approx_swap_free(global);
    }

    out = snap;
    return true;
}

bool WinMetricsSource::sample_system(SystemSnapshot& out) const {
    MEMORYSTATUSEX global{};
    if (!read_global_memory(global)) {
        return false;
    }

    SystemSnapshot snap;
    snap.mem_total = global.ullTotalPhys;
    snap.mem_available = global.ullAvailPhys;
    snap.mem_used = global.ullTotalPhys > global.ullAvailPhys
                        ? global.ullTotalPhys - global.ullAvailPhys
                        : 0;
    snap.swap_total = approx_swap_total(global);
    snap.swap_free = approx_swap_free(global);
    snap.swap_used = snap.swap_total > snap.swap_free
                         ? snap.swap_total - snap.swap_free
                         : 0;
    snap.swap_available = snap.swap_total > 0;
    // Windows no expone load average ni contadores equivalentes a /proc/stat;
    // la UI los muestra como no disponibles.
    out = snap;
    return true;
}

SystemInfo WinMetricsSource::system_info() const {
    SystemInfo info;
    MEMORYSTATUSEX global{};
    if (read_global_memory(global)) {
        info.total_ram = global.ullTotalPhys;
        info.available_ram = global.ullAvailPhys;
        info.swap_total = approx_swap_total(global);
    }
    const DWORD cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    info.cores = cores > 0 ? static_cast<int>(cores) : 1;
    return info;
}

}  // namespace memtest::metrics

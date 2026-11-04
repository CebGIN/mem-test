#include "core/SystemInfo.hpp"

#include <sys/sysinfo.h>
#include <unistd.h>

#include "metrics/ProcFs.hpp"

namespace memtest {

SystemInfo read_system_info() {
    SystemInfo info;

    // /proc/meminfo da MemAvailable, que es la referencia útil para los
    // guardrails; sysinfo(2) queda como respaldo si el archivo no se puede leer.
    metrics::MemInfo mem;
    if (metrics::read_meminfo(mem)) {
        info.total_ram = mem.total_bytes;
        info.available_ram = mem.available_bytes;
        info.swap_total = mem.swap_total_bytes;
    } else {
        struct sysinfo raw {};
        if (::sysinfo(&raw) == 0) {
            const std::uint64_t unit = raw.mem_unit > 0 ? raw.mem_unit : 1;
            info.total_ram = raw.totalram * unit;
            info.available_ram = raw.freeram * unit;
            info.swap_total = raw.totalswap * unit;
        }
    }

    const long cores = ::sysconf(_SC_NPROCESSORS_ONLN);
    info.cores = cores > 0 ? static_cast<int>(cores) : 1;
    return info;
}

}  // namespace memtest

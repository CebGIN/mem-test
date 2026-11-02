#ifndef MEMTEST_METRICS_PROCFS_HPP
#define MEMTEST_METRICS_PROCFS_HPP

#include <cstdint>
#include <string>

#include <sys/types.h>

namespace memtest::metrics {

// Campos de /proc/<pid>/stat que usa el monitor. Los índices corresponden a la
// numeración del kernel; `comm` se extrae aparte porque puede contener espacios
// y paréntesis.
struct ProcStat {
    std::string comm;               // campo 2
    char state = '?';               // campo 3
    int ppid = 0;                   // campo 4
    std::uint64_t minflt = 0;       // campo 10
    std::uint64_t majflt = 0;       // campo 12
    std::uint64_t utime_ticks = 0;  // campo 14
    std::uint64_t stime_ticks = 0;  // campo 15
    long nice = 0;                  // campo 19
    long num_threads = 0;           // campo 20
    std::uint64_t vsize_bytes = 0;  // campo 23
    long rss_pages = 0;             // campo 24
};

struct MemInfo {
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t available_bytes = 0;
    std::uint64_t buffers_bytes = 0;
    std::uint64_t cached_bytes = 0;
    std::uint64_t swap_total_bytes = 0;
    std::uint64_t swap_free_bytes = 0;
};

std::uint64_t clock_ticks_per_second();
std::uint64_t ticks_to_ns(std::uint64_t ticks);
std::uint64_t page_size_bytes();

// Lee un archivo completo. Devuelve false si no existe o no se puede abrir.
bool read_file(const std::string& path, std::string& out);

// Parsea el contenido de /proc/<pid>/stat. Devuelve false si el formato no
// alcanza los campos necesarios.
bool parse_stat(const std::string& data, ProcStat& out);

bool read_proc_stat(pid_t pid, ProcStat& out);

// Parsea /proc/meminfo y /proc/loadavg. Si falta MemAvailable, se aproxima con
// MemFree + Buffers + Cached.
bool read_meminfo(MemInfo& out);
bool read_load_average(double& load_1m);

}  // namespace memtest::metrics

#endif  // MEMTEST_METRICS_PROCFS_HPP

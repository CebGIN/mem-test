#ifndef MEMTEST_METRICS_PROCMETRICSSOURCE_HPP
#define MEMTEST_METRICS_PROCMETRICSSOURCE_HPP

// Fuente de métricas para Linux: combina /proc/<pid>/status, /proc/<pid>/stat,
// /proc/meminfo y /proc/loadavg.

#include <cstdint>
#include <string>

#include "metrics/IMetricsSource.hpp"

namespace memtest::metrics {

// Campos de /proc/<pid>/status que usa el monitor. Todos en bytes salvo los
// contadores. Un campo ausente (kernel viejo, hilo del kernel, zombi) queda en
// cero; los `has_*` distinguen "ausente" de "cero real" donde importa.
struct ProcStatus {
    std::uint64_t vm_size = 0;      // VmSize
    std::uint64_t vm_rss = 0;       // VmRSS
    std::uint64_t vm_hwm = 0;       // VmHWM
    std::uint64_t vm_swap = 0;      // VmSwap (kernel >= 2.6.34)
    std::uint64_t rss_anon = 0;     // RssAnon (kernel >= 4.5)
    std::uint64_t rss_file = 0;     // RssFile (kernel >= 4.5)
    std::uint64_t rss_shmem = 0;    // RssShmem (kernel >= 4.5)
    long threads = 0;               // Threads
    std::uint64_t voluntary_ctxt = 0;    // voluntary_ctxt_switches
    std::uint64_t involuntary_ctxt = 0;  // nonvoluntary_ctxt_switches
    bool has_vm_size = false;
    bool has_vm_rss = false;
    bool has_threads = false;
};

// Parsea el contenido de /proc/<pid>/status. Las líneas son `Clave:\tvalor [kB]`;
// los tamaños en kB se convierten a bytes. Devuelve false si un valor de las
// claves que usamos no es un entero válido (o se desborda al convertir a bytes)
// o si el texto no contiene ni `Threads` ni `VmRSS` (no parece un status).
bool parse_status(const std::string& data, ProcStatus& out);

bool read_proc_status(pid_t pid, ProcStatus& out);

class ProcMetricsSource final : public IMetricsSource {
public:
    bool sample(pid_t pid, MetricsSnapshot& out) const override;
    SystemInfo system_info() const override;
};

}  // namespace memtest::metrics

#endif  // MEMTEST_METRICS_PROCMETRICSSOURCE_HPP

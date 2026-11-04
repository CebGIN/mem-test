#ifndef MEMTEST_METRICS_WINMETRICSSOURCE_HPP
#define MEMTEST_METRICS_WINMETRICSSOURCE_HPP

// Fuente de métricas para Windows (psapi, GetProcessTimes, Toolhelp,
// GlobalMemoryStatusEx). Windows no expone todo lo que da /proc; lo que no
// existe queda en 0 y la UI debe tratarlo como "no disponible":
//
//   vm_size      = bytes comprometidos privados (PrivateUsage); nunca < RSS
//   vm_rss       = working set
//   vm_hwm       = pico del working set
//   vm_swap      = 0 (Windows no da el volumen paginado por proceso)
//   rss_*        = 0 (sin desglose anon/file/shmem)
//   minor_faults = PageFaultCount (incluye los duros; no se pueden separar)
//   major_faults = 0
//   *_ctxt       = 0
//   load_1m      = 0 (Windows no tiene load average)
//   sys_swap_*   = archivo de paginación aproximado: límite de commit - RAM

#include "metrics/IMetricsSource.hpp"

namespace memtest::metrics {

class WinMetricsSource final : public IMetricsSource {
public:
    bool sample(pid_t pid, MetricsSnapshot& out) const override;
    SystemInfo system_info() const override;
};

}  // namespace memtest::metrics

#endif  // MEMTEST_METRICS_WINMETRICSSOURCE_HPP

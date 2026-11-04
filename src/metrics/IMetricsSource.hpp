#ifndef MEMTEST_METRICS_IMETRICSSOURCE_HPP
#define MEMTEST_METRICS_IMETRICSSOURCE_HPP

#include <sys/types.h>

#include "domain/Types.hpp"

namespace memtest {

// Fuente de métricas del proceso observado. El padre muestrea /proc/<pid>; el
// worker nunca se instrumenta (Arquitectura.md §8).
class IMetricsSource {
public:
    virtual ~IMetricsSource() = default;
    virtual bool sample(pid_t pid, MetricsSnapshot& out) const = 0;
    virtual SystemInfo system_info() const = 0;
};

}  // namespace memtest

#endif  // MEMTEST_METRICS_IMETRICSSOURCE_HPP

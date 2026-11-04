// PROVISIONAL (WP1): contrato de Arquitectura.md §5. El dueño es el Lead; si su
// WP0 ya trae este archivo, usar el suyo.
#ifndef MEMTEST_METRICS_IMETRICSSOURCE_HPP
#define MEMTEST_METRICS_IMETRICSSOURCE_HPP

#include "domain/Types.hpp"
#include "platform/Pid.hpp"

namespace memtest {

class IMetricsSource {
public:
    virtual ~IMetricsSource() = default;
    // Rellena `out` con una muestra del proceso `pid`. Devuelve false si el
    // proceso ya no existe o no se puede leer. No toca `out.elapsed`: lo pone
    // quien conoce el instante de inicio de la ejecución.
    virtual bool sample(pid_t pid, MetricsSnapshot& out) const = 0;
    virtual SystemInfo system_info() const = 0;
};

}  // namespace memtest

#endif  // MEMTEST_METRICS_IMETRICSSOURCE_HPP

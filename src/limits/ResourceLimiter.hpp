#ifndef MEMTEST_LIMITS_RESOURCELIMITER_HPP
#define MEMTEST_LIMITS_RESOURCELIMITER_HPP

#include <string>

#include "domain/Types.hpp"

namespace memtest::limits {

// Aplica al PROCESO ACTUAL los límites pedidos en un ExperimentConfig. Está
// pensado para que lo llame el worker al arrancar, antes de ejecutar la carga
// (Arquitectura.md §9).
//
//   Linux    memoria  setrlimit(RLIMIT_AS | RLIMIT_DATA), soft = hard
//            CPU      setrlimit(RLIMIT_CPU), soft = N, hard = N + 1
//   Windows  memoria  Job Object con JOB_OBJECT_LIMIT_PROCESS_MEMORY (límite
//                     de memoria comprometida; vale para AS y DATA)
//            CPU      Job Object con JOB_OBJECT_LIMIT_PROCESS_TIME = N + 1
//                     (solo tiempo de usuario); el "blando" N lo avisa
//                     install_cpu_limit_notifier()
//
// Nunca aborta: si un límite no se puede aplicar, queda NotApplied (o
// Unsupported si la plataforma no lo ofrece) y la ejecución sigue; la UI no
// debe presentar esa prueba como limitada.
class ResourceLimiter {
public:
    // `detail`, si no es null, recibe una explicación legible de lo que pasó
    // (errores del sistema, equivalencias entre plataformas). Puede quedar vacío.
    LimitReport apply(const ExperimentConfig& config,
                      std::string* detail = nullptr) const;
};

}  // namespace memtest::limits

#endif  // MEMTEST_LIMITS_RESOURCELIMITER_HPP

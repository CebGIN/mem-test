#ifndef MEMTEST_LIMITS_PLATFORMLIMITS_HPP
#define MEMTEST_LIMITS_PLATFORMLIMITS_HPP

// Interno de limits/: funciones que implementa cada plataforma. No incluir
// desde fuera.

#include <string>

#include "domain/Types.hpp"

namespace memtest::limits::detail {

// Rellenan memory_status/memory_applied o cpu_seconds_status/cpu_seconds_applied
// en `report` y añaden a `notes` lo que merezca explicarse.
void apply_memory_limit(MemoryLimitKind kind, std::uint64_t bytes,
                        LimitReport& report, std::string& notes);
void apply_cpu_seconds_limit(int seconds, LimitReport& report,
                             std::string& notes);

}  // namespace memtest::limits::detail

#endif  // MEMTEST_LIMITS_PLATFORMLIMITS_HPP

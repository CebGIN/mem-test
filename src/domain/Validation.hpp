#ifndef MEMTEST_DOMAIN_VALIDATION_HPP
#define MEMTEST_DOMAIN_VALIDATION_HPP

#include <cstdint>

#include "domain/Types.hpp"

namespace memtest {

// Topes de software calculados una vez al iniciar (Arquitectura.md §10). La UI
// los muestra y la validación los aplica; no son editables en el MVP.
struct GuardrailLimits {
    std::uint64_t max_memory_bytes = 0;  // 50% de RAM total, sin tope absoluto
    int max_threads = 0;                 // min(8 x núcleos, 64)
    int max_wall_timeout_ms = 0;         // 120 s
    int min_cpu_seconds = 1;
    int max_cpu_seconds = 120;
};

// Deriva los topes del equipo. Si `info` viene vacío se usan valores
// conservadores (4 GiB de RAM, 1 núcleo) para no bloquear la aplicación.
GuardrailLimits guardrails_from(const SystemInfo& info);

// Validación pura de configuración: no lee /proc, no lanza procesos y no
// bloquea. Devuelve una lista de problemas; vacía significa configuración
// aceptable.
ValidationReport validate(const ExperimentConfig& config, const GuardrailLimits& limits);

}  // namespace memtest

#endif  // MEMTEST_DOMAIN_VALIDATION_HPP

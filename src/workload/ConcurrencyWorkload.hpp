#ifndef MEMTEST_WORKLOAD_CONCURRENCYWORKLOAD_HPP
#define MEMTEST_WORKLOAD_CONCURRENCYWORKLOAD_HPP

#include "workload/IWorkload.hpp"

namespace memtest {

// Carga CPU-bound determinista. Cada tarea ejecuta `work_units` iteraciones de
// un hash entero y acumula un checksum local (Arquitectura.md §12.1). El
// resultado compara configuraciones; no hay un valor "correcto" que exigir.
class ConcurrencyWorkload final : public IWorkload {
public:
    WorkloadOutcome run(const WorkloadContext& context) override;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_CONCURRENCYWORKLOAD_HPP

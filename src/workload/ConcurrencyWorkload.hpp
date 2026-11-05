#ifndef MEMTEST_WORKLOAD_CONCURRENCYWORKLOAD_HPP
#define MEMTEST_WORKLOAD_CONCURRENCYWORKLOAD_HPP

#include "workload/IWorkload.hpp"

namespace memtest {

// Carga CPU-bound determinista. Con `LoadMode::TotalWork` reparte `work_units`
// unidades entre las tareas; con `LoadMode::Duration` corre hasta el deadline
// (o mantiene el modo historico por tarea si no hay duracion). Cada tarea
// acumula un checksum local (Arquitectura.md §12.1). El resultado compara
// configuraciones; no hay un valor "correcto" que exigir.
class ConcurrencyWorkload final : public IWorkload {
public:
    WorkloadOutcome run(const WorkloadContext& context) override;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_CONCURRENCYWORKLOAD_HPP

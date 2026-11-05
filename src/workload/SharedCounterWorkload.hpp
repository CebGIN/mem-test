#ifndef MEMTEST_WORKLOAD_SHAREDCOUNTERWORKLOAD_HPP
#define MEMTEST_WORKLOAD_SHAREDCOUNTERWORKLOAD_HPP

#include "workload/IWorkload.hpp"

namespace memtest {

// Varias tareas incrementan un contador compartido con o sin sincronizacion
// (Arquitectura.md §12.4). `None` usa un contador no atomico y provoca un data
// race real, de modo que las actualizaciones perdidas no son deterministas;
// eso es el resultado observado, no un fallo. `Mutex` y `Atomic` son exactos.
class SharedCounterWorkload final : public IWorkload {
public:
    WorkloadOutcome run(const WorkloadContext& context) override;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_SHAREDCOUNTERWORKLOAD_HPP

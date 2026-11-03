#ifndef MEMTEST_WORKLOAD_SHAREDCOUNTERWORKLOAD_HPP
#define MEMTEST_WORKLOAD_SHAREDCOUNTERWORKLOAD_HPP

#include "workload/IWorkload.hpp"

namespace memtest {

// Varias tareas incrementan un contador compartido con o sin sincronizacion
// (Arquitectura.md §12.4). `None` usa load/store relajados y puede perder
// actualizaciones; eso es el resultado observado, no un fallo.
class SharedCounterWorkload final : public IWorkload {
public:
    WorkloadOutcome run(const WorkloadContext& context) override;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_SHAREDCOUNTERWORKLOAD_HPP

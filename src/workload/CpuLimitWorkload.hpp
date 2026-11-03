#ifndef MEMTEST_WORKLOAD_CPULIMITWORKLOAD_HPP
#define MEMTEST_WORKLOAD_CPULIMITWORKLOAD_HPP

#include "workload/IWorkload.hpp"

namespace memtest {

// Carga CPU-bound sin reservas grandes. El corte real lo hace el kernel con
// RLIMIT_CPU; el workload solo responde a la cancelacion cooperativa
// (Arquitectura.md §12.2).
class CpuLimitWorkload final : public IWorkload {
public:
    WorkloadOutcome run(const WorkloadContext& context) override;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_CPULIMITWORKLOAD_HPP

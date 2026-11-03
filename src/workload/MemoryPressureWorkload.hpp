#ifndef MEMTEST_WORKLOAD_MEMORYPRESSUREWORKLOAD_HPP
#define MEMTEST_WORKLOAD_MEMORYPRESSUREWORKLOAD_HPP

#include "workload/IWorkload.hpp"

namespace memtest {

// Reserva memoria virtual anonima y toca una pagina cada `page_stride_bytes`
// para forzar fallos de pagina y residency (Arquitectura.md §12.3). Si mmap
// falla con ENOMEM el resultado es MemoryLimitHit.
class MemoryPressureWorkload final : public IWorkload {
public:
    WorkloadOutcome run(const WorkloadContext& context) override;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_MEMORYPRESSUREWORKLOAD_HPP

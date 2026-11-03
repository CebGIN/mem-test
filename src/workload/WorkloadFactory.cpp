#include "workload/IWorkload.hpp"

#include "workload/ConcurrencyWorkload.hpp"
#include "workload/CpuLimitWorkload.hpp"
#include "workload/MemoryPressureWorkload.hpp"
#include "workload/SharedCounterWorkload.hpp"

namespace memtest {

std::unique_ptr<IWorkload> make_workload(WorkloadKind kind) {
    switch (kind) {
        case WorkloadKind::Concurrency:
            return std::make_unique<ConcurrencyWorkload>();
        case WorkloadKind::CpuLimit:
            return std::make_unique<CpuLimitWorkload>();
        case WorkloadKind::MemoryPressure:
            return std::make_unique<MemoryPressureWorkload>();
        case WorkloadKind::SharedCounter:
            return std::make_unique<SharedCounterWorkload>();
    }
    return nullptr;
}

}  // namespace memtest

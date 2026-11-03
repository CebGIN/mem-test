#include "workload/CpuLimitWorkload.hpp"

#include "workload/ConcurrencyWorkload.hpp"

namespace memtest {

WorkloadOutcome CpuLimitWorkload::run(const WorkloadContext& context) {
    // La carga es la misma que la de concurrencia: CPU-bound, determinista y
    // sin allocaciones grandes. Lo que cambia es el limite RLIMIT_CPU que
    // aplica el worker; SIGXCPU llega como cancelacion cooperativa.
    ConcurrencyWorkload workload;
    WorkloadOutcome result = workload.run(context);
    if (result.outcome != RunOutcome::Error) {
        result.detail = "carga_cpu " + result.detail;
    }
    return result;
}

}  // namespace memtest

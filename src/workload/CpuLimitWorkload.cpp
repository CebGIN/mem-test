#include "workload/CpuLimitWorkload.hpp"

#include "workload/ConcurrencyWorkload.hpp"

namespace memtest {

WorkloadOutcome CpuLimitWorkload::run(const WorkloadContext& context) {
    if (context.config == nullptr ||
        (!context.config->cpu_limit.seconds.has_value() &&
         !context.config->cpu_limit.quota_percent.has_value())) {
        WorkloadOutcome result;
        result.outcome = RunOutcome::Error;
        result.detail =
            "la prueba de limite de CPU requiere RLIMIT_CPU o cuota cgroup";
        return result;
    }

    // La carga es la misma que la de concurrencia: CPU-bound, determinista y
    // sin allocaciones grandes. Lo que cambia es el limite que aplica el
    // worker; SIGXCPU llega como cancelacion cooperativa.
    ConcurrencyWorkload workload;
    WorkloadOutcome result = workload.run(context);
    if (result.outcome != RunOutcome::Error) {
        result.detail = "carga_cpu " + result.detail;
    }
    return result;
}

}  // namespace memtest

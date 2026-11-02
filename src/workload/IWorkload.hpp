#ifndef MEMTEST_WORKLOAD_IWORKLOAD_HPP
#define MEMTEST_WORKLOAD_IWORKLOAD_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "domain/Types.hpp"
#include "runtime/CancellationToken.hpp"

namespace memtest {

// Contexto de una ejecución de workload dentro del worker.
struct WorkloadContext {
    const ExperimentConfig* config;
    const CancellationToken* cancel;
    std::function<void(std::uint64_t done, std::uint64_t total)> progress;
};

struct WorkloadOutcome {
    RunOutcome outcome = RunOutcome::Completed;
    std::uint64_t expected = 0;
    std::uint64_t observed = 0;
    std::string detail;
};

class IWorkload {
public:
    virtual ~IWorkload() = default;
    virtual WorkloadOutcome run(const WorkloadContext& context) = 0;
};

std::unique_ptr<IWorkload> make_workload(WorkloadKind kind);

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_IWORKLOAD_HPP

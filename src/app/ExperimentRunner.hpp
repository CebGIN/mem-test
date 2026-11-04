#ifndef MEMTEST_APP_EXPERIMENTRUNNER_HPP
#define MEMTEST_APP_EXPERIMENTRUNNER_HPP

#include <functional>

#include "domain/Types.hpp"
#include "process/IProcessSupervisor.hpp"
#include "runtime/CancellationToken.hpp"

namespace memtest {

class IMetricsSource;

// Ejecuta una sola corrida: lanza el worker, muestrea /proc y arma el
// ExperimentResult. La cancelación se observa por el token; el runner se
// encarga de pedir SIGTERM y de escalar a SIGKILL.
class ExperimentRunner {
public:
    using SampleCallback = std::function<void(const MetricsSnapshot&)>;
    using EventCallback = std::function<void(const WorkerEvent&)>;

    ExperimentRunner(IMetricsSource& metrics, IProcessSupervisor& supervisor);

    ExperimentRunner(const ExperimentRunner&) = delete;
    ExperimentRunner& operator=(const ExperimentRunner&) = delete;

    ExperimentResult run(const ExperimentConfig& config,
                         const CancellationToken& cancel,
                         SampleCallback on_sample = {},
                         EventCallback on_event = {});

private:
    IMetricsSource& metrics_;
    IProcessSupervisor& supervisor_;
};

}  // namespace memtest

#endif  // MEMTEST_APP_EXPERIMENTRUNNER_HPP

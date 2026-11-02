#ifndef MEMTEST_APP_IEXPERIMENTCONTROLLER_HPP
#define MEMTEST_APP_IEXPERIMENTCONTROLLER_HPP

#include <filesystem>
#include <string>
#include <vector>

#include "domain/Types.hpp"

namespace memtest {

// Estado que la UI lee una vez por frame. `recent` guarda las últimas muestras
// para la gráfica.
struct LiveState {
    RunState state = RunState::Idle;
    MetricsSnapshot last;
    std::vector<MetricsSnapshot> recent;
    std::string message;
};

// Única puerta de la UI hacia el núcleo (Arquitectura.md §5). La UI no incluye
// process/, metrics/, limits/ ni workload/.
class IExperimentController {
public:
    virtual ~IExperimentController() = default;
    virtual ValidationReport validate(const ExperimentConfig& config) const = 0;
    virtual bool start(const ExperimentConfig& config, std::string* error) = 0;
    virtual void stop() = 0;
    virtual LiveState live() const = 0;
    virtual std::vector<ExperimentResult> take_results() = 0;
    virtual std::vector<ExperimentResult> history() const = 0;
    virtual bool export_csv(const std::filesystem::path& dir, std::string* error) = 0;
};

}  // namespace memtest

#endif  // MEMTEST_APP_IEXPERIMENTCONTROLLER_HPP

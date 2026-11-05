#ifndef MEMTEST_APP_EXPERIMENTCONTROLLER_HPP
#define MEMTEST_APP_EXPERIMENTCONTROLLER_HPP

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/IExperimentController.hpp"
#include "core/ResultStore.hpp"
#include "domain/Validation.hpp"
#include "runtime/CancellationToken.hpp"

namespace memtest {

class IMetricsSource;
class IProcessSupervisor;

// Controlador real: lanza un ExperimentRunner en un hilo, publica LiveState y
// encola los resultados para la UI (Arquitectura.md §5 y §11).
class ExperimentController final : public IExperimentController {
public:
    ExperimentController(std::unique_ptr<IMetricsSource> metrics,
                         std::unique_ptr<IProcessSupervisor> supervisor,
                         SystemInfo info = {});
    ~ExperimentController() override;

    ExperimentController(const ExperimentController&) = delete;
    ExperimentController& operator=(const ExperimentController&) = delete;

    ValidationReport validate(const ExperimentConfig& config) const override;
    bool start(const ExperimentConfig& config, std::string* error) override;
    void stop() override;
    LiveState live() override;
    std::vector<ExperimentResult> take_results() override;
    std::vector<ExperimentResult> history() const override;
    bool export_csv(const std::filesystem::path& dir,
                    std::string* error) override;

private:
    void run(ExperimentConfig config);
    void sample_system_locked(bool force = false);
    void update_system_impact_locked(ExperimentResult& result);

    std::unique_ptr<IMetricsSource> metrics_;
    std::unique_ptr<IProcessSupervisor> supervisor_;

    mutable std::mutex mutex_;
    LiveState live_;
    std::deque<ExperimentResult> pending_;
    ResultStore history_;
    std::thread runner_;
    std::unique_ptr<CancellationToken> token_;
    SystemInfo system_;
    GuardrailLimits limits_;
    bool running_ = false;
    bool stop_requested_ = false;

    // Muestreo global del sistema. La UI llama a live() por frame; se toma una
    // muestra nueva cada 500 ms y se guarda una serie para las gráficas.
    std::chrono::steady_clock::time_point last_system_sample_{};
    bool system_sampled_ = false;
    std::chrono::steady_clock::time_point monitor_start_{};
    std::vector<SystemSnapshot> run_system_samples_;
};

}  // namespace memtest

#endif  // MEMTEST_APP_EXPERIMENTCONTROLLER_HPP

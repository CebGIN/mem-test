#ifndef MEMTEST_APP_FAKEEXPERIMENTCONTROLLER_HPP
#define MEMTEST_APP_FAKEEXPERIMENTCONTROLLER_HPP

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/IExperimentController.hpp"
#include "domain/Types.hpp"
#include "domain/Validation.hpp"

namespace memtest {

// Controlador simulado: permite que la UI, el historial y el CSV se desarrollen
// sin lanzar workers. Genera una serie determinista cada 250 ms y no lee
// /proc. Los límites que reporta son simulados; el worker real llega en WP4.
class FakeExperimentController : public IExperimentController {
public:
    FakeExperimentController();
    explicit FakeExperimentController(SystemInfo info);
    ~FakeExperimentController() override;

    FakeExperimentController(const FakeExperimentController&) = delete;
    FakeExperimentController& operator=(const FakeExperimentController&) = delete;

    ValidationReport validate(const ExperimentConfig& config) const override;
    bool start(const ExperimentConfig& config, std::string* error) override;
    void stop() override;
    LiveState live() const override;
    std::vector<ExperimentResult> take_results() override;
    std::vector<ExperimentResult> history() const override;
    bool export_csv(const std::filesystem::path& dir, std::string* error) override;

private:
    void run(ExperimentConfig config, std::uint64_t run_number);
    bool wait_for(std::chrono::milliseconds period);
    MetricsSnapshot make_sample(const ExperimentConfig& config, std::size_t index,
                                std::size_t samples) const;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    LiveState live_;
    std::deque<ExperimentResult> pending_;
    std::vector<ExperimentResult> history_;
    std::thread runner_;
    SystemInfo system_;
    GuardrailLimits limits_;
    std::uint64_t next_run_number_ = 1;
    std::uint64_t running_run_ = 0;  // 0 = no hay ejecución en curso
    bool stop_requested_ = false;
};

}  // namespace memtest

#endif  // MEMTEST_APP_FAKEEXPERIMENTCONTROLLER_HPP

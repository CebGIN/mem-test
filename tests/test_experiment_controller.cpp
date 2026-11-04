#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "Suites.hpp"
#include "app/ExperimentController.hpp"
#include "metrics/MetricsSourceFactory.hpp"
#include "process/PosixProcessSupervisor.hpp"

namespace memtest::test {
namespace {

using namespace std::chrono_literals;

std::string worker_path() {
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return "mem-test-worker";
    }
    return (self.parent_path() / "mem-test-worker").string();
}

std::unique_ptr<ExperimentController> make_controller() {
    return std::make_unique<ExperimentController>(
        make_metrics_source(),
        std::make_unique<PosixProcessSupervisor>(worker_path()));
}

bool wait_done(ExperimentController& controller,
               std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (controller.live().state == RunState::Done) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return controller.live().state == RunState::Done;
}

void completes_a_real_run(Context& context) {
    auto controller = make_controller();
    ExperimentConfig config;
    config.threads = 1;
    config.work_units = 1000;
    config.wall_timeout_ms = 5000;

    std::string error;
    CHECK(context, controller->start(config, &error));
    CHECK(context, error.empty());
    CHECK(context, controller->live().state != RunState::Done);
    CHECK(context, wait_done(*controller, 5000ms));

    const auto results = controller->take_results();
    CHECK_EQ(context, results.size(), std::size_t{1});
    if (!results.empty()) {
        const ExperimentResult& result = results.front();
        CHECK_EQ(context, result.outcome, RunOutcome::Completed);
        CHECK_EQ(context, result.exit_code, 0);
        CHECK(context, result.term_signal == 0);
        CHECK(context, !result.series.empty());
        CHECK(context, result.last.vm_rss > 0);
        CHECK(context, result.duration.count() >= 0);
    }
    CHECK_EQ(context, controller->history().size(), std::size_t{1});
    CHECK(context, controller->take_results().empty());
}

void stop_is_cooperative(Context& context) {
    auto controller = make_controller();
    ExperimentConfig config;
    config.threads = 1;
    config.work_units = 1000000000ull;
    config.wall_timeout_ms = 120000;

    std::string error;
    CHECK(context, controller->start(config, &error));
    CHECK(context, error.empty());
    std::this_thread::sleep_for(100ms);

    const auto started = std::chrono::steady_clock::now();
    controller->stop();
    CHECK(context, wait_done(*controller, 3000ms));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    CHECK(context, elapsed < std::chrono::milliseconds(1500));

    const auto results = controller->take_results();
    CHECK_EQ(context, results.size(), std::size_t{1});
    if (!results.empty()) {
        CHECK_EQ(context, results.front().outcome, RunOutcome::Stopped);
        CHECK_EQ(context, results.front().exit_code, 12);
        CHECK(context, !results.front().series.empty());
    }
}

void rejects_second_start(Context& context) {
    auto controller = make_controller();
    ExperimentConfig config;
    config.threads = 1;
    config.work_units = 1000000000ull;
    config.wall_timeout_ms = 120000;

    std::string error;
    CHECK(context, controller->start(config, &error));
    std::string second_error;
    CHECK(context, !controller->start(config, &second_error));
    CHECK(context, !second_error.empty());

    controller->stop();
    CHECK(context, wait_done(*controller, 3000ms));
}

void rejects_invalid_config(Context& context) {
    auto controller = make_controller();
    ExperimentConfig config;
    config.threads = 0;

    std::string error;
    CHECK(context, !controller->start(config, &error));
    CHECK(context, !error.empty());
    CHECK_EQ(context, controller->live().state, RunState::Idle);
    CHECK(context, controller->take_results().empty());
}

}  // namespace

Suite experiment_controller_suite() {
    return {
        {"ejecucion real completa", completes_a_real_run},
        {"stop cooperativo", stop_is_cooperative},
        {"rechaza segundo start", rejects_second_start},
        {"rechaza configuracion invalida", rejects_invalid_config},
    };
}

}  // namespace memtest::test

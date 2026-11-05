#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "Suites.hpp"
#include "app/FakeExperimentController.hpp"

namespace memtest::test {
namespace {

using namespace std::chrono_literals;

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

SystemInfo test_system() {
    SystemInfo info;
    info.total_ram = 8 * 1024 * kMiB;
    info.available_ram = 6 * 1024 * kMiB;
    info.swap_total = 0;
    info.cores = 4;
    return info;
}

bool wait_for_done(FakeExperimentController& controller, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (controller.live().state == RunState::Done) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return controller.live().state == RunState::Done;
}

std::filesystem::path temp_dir(const std::string& tag) {
    static std::atomic<std::uint64_t> counter{1};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("memtest-" + tag + "-" + std::to_string(stamp) + "-" +
            std::to_string(counter.fetch_add(1)));
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void test_validate_uses_guardrails(Context& ctx) {
    FakeExperimentController controller(test_system());

    ExperimentConfig config;
    config.threads = 1000;
    CHECK(ctx, !controller.validate(config).ok());
    CHECK(ctx, controller.validate(ExperimentConfig{}).ok());
}

void test_start_rejects_invalid(Context& ctx) {
    FakeExperimentController controller(test_system());

    ExperimentConfig config;
    config.threads = 0;
    std::string error;
    CHECK(ctx, !controller.start(config, &error));
    CHECK(ctx, !error.empty());
    CHECK_EQ(ctx, controller.live().state, RunState::Idle);
    CHECK(ctx, controller.take_results().empty());
}

void test_start_is_nonblocking_and_completes(Context& ctx) {
    FakeExperimentController controller(test_system());

    ExperimentConfig config;
    config.threads = 2;
    config.work_units = 5000;
    config.wall_timeout_ms = 300;

    std::string error;
    CHECK(ctx, controller.start(config, &error));
    CHECK(ctx, error.empty());
    CHECK(ctx, controller.live().state != RunState::Done);

    CHECK(ctx, wait_for_done(controller, 3000ms));

    const std::vector<ExperimentResult> results = controller.take_results();
    CHECK_EQ(ctx, results.size(), std::size_t{1});
    if (!results.empty()) {
        const ExperimentResult& result = results.front();
        CHECK_EQ(ctx, result.outcome, RunOutcome::Completed);
        CHECK_EQ(ctx, result.config.threads, 2);
        CHECK(ctx, !result.series.empty());
        CHECK(ctx, result.last.vm_rss > 0);
        CHECK(ctx, result.duration.count() > 0);
        CHECK_EQ(ctx, result.exit_code, 0);
        CHECK(ctx, result.series.size() <= 2000);
    }

    // La cola se drena: una segunda lectura no repite el resultado.
    CHECK(ctx, controller.take_results().empty());
    CHECK_EQ(ctx, controller.history().size(), std::size_t{1});
}

void test_stop_resolves_quickly(Context& ctx) {
    FakeExperimentController controller(test_system());

    ExperimentConfig config;
    config.wall_timeout_ms = 5000;

    std::string error;
    CHECK(ctx, controller.start(config, &error));
    std::this_thread::sleep_for(50ms);
    controller.stop();
    CHECK(ctx, wait_for_done(controller, 2000ms));

    const std::vector<ExperimentResult> results = controller.take_results();
    CHECK_EQ(ctx, results.size(), std::size_t{1});
    if (!results.empty()) {
        CHECK_EQ(ctx, results.front().outcome, RunOutcome::Stopped);
        CHECK_EQ(ctx, results.front().exit_code, 12);
        CHECK(ctx, results.front().duration.count() < 2000);
        CHECK(ctx, !results.front().series.empty());
    }
}

void test_start_while_running_is_rejected(Context& ctx) {
    FakeExperimentController controller(test_system());

    ExperimentConfig config;
    config.wall_timeout_ms = 1000;

    std::string error;
    CHECK(ctx, controller.start(config, &error));

    std::string second_error;
    CHECK(ctx, !controller.start(config, &second_error));
    CHECK(ctx, !second_error.empty());

    controller.stop();
    CHECK(ctx, wait_for_done(controller, 2000ms));
}

void test_shared_counter_reports_losses(Context& ctx) {
    FakeExperimentController controller(test_system());

    ExperimentConfig config;
    config.workload = WorkloadKind::SharedCounter;
    config.threads = 4;
    config.increments_per_thread = 100000;
    config.wall_timeout_ms = 120;

    std::string error;
    config.sync_mode = SyncMode::None;
    CHECK(ctx, controller.start(config, &error));
    CHECK(ctx, wait_for_done(controller, 2000ms));
    std::vector<ExperimentResult> results = controller.take_results();
    CHECK_EQ(ctx, results.size(), std::size_t{1});
    if (!results.empty()) {
        CHECK_EQ(ctx, results.front().expected_value, std::uint64_t{400000});
        CHECK(ctx, results.front().observed_value < results.front().expected_value);
    }

    config.sync_mode = SyncMode::Atomic;
    CHECK(ctx, controller.start(config, &error));
    CHECK(ctx, wait_for_done(controller, 2000ms));
    results = controller.take_results();
    CHECK_EQ(ctx, results.size(), std::size_t{1});
    if (!results.empty()) {
        CHECK_EQ(ctx, results.front().observed_value, results.front().expected_value);
    }
}

void test_memory_pressure_limit_hit(Context& ctx) {
    FakeExperimentController controller(test_system());

    ExperimentConfig config;
    config.workload = WorkloadKind::MemoryPressure;
    config.memory_bytes = 64 * kMiB;
    config.page_stride_bytes = 4096;
    config.memory_limit_kind = MemoryLimitKind::AddressSpace;
    config.memory_limit_bytes = 64 * kMiB;
    config.wall_timeout_ms = 120;

    std::string error;
    CHECK(ctx, controller.start(config, &error));
    CHECK(ctx, wait_for_done(controller, 2000ms));

    const std::vector<ExperimentResult> results = controller.take_results();
    CHECK_EQ(ctx, results.size(), std::size_t{1});
    if (!results.empty()) {
        CHECK_EQ(ctx, results.front().outcome, RunOutcome::MemoryLimitHit);
        CHECK_EQ(ctx, results.front().exit_code, 10);
        CHECK_EQ(ctx, results.front().limits.memory_status, LimitStatus::Applied);
        CHECK_EQ(ctx, results.front().expected_value, std::uint64_t{16384});
        CHECK(ctx, results.front().observed_value <= results.front().expected_value);
    }
}

void test_export_csv_writes_files(Context& ctx) {
    FakeExperimentController controller(test_system());
    const std::filesystem::path dir = temp_dir("fake-csv");

    std::string error;
    CHECK(ctx, !controller.export_csv(dir, &error));
    CHECK(ctx, !error.empty());

    ExperimentConfig config;
    config.threads = 2;
    config.work_units = 1000;
    config.wall_timeout_ms = 120;

    CHECK(ctx, controller.start(config, &error));
    CHECK(ctx, wait_for_done(controller, 2000ms));
    controller.take_results();

    CHECK(ctx, controller.export_csv(dir, &error));
    CHECK(ctx, error.empty());

    std::filesystem::path summary;
    std::filesystem::path detail;
    std::size_t summaries = 0;
    std::size_t details = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("resumen_", 0) == 0) {
            ++summaries;
            summary = entry.path();
        } else if (name.rfind("detalle_", 0) == 0) {
            ++details;
            detail = entry.path();
        }
    }
    CHECK_EQ(ctx, summaries, std::size_t{1});
    CHECK_EQ(ctx, details, std::size_t{1});

    const std::string summary_text = read_file(summary);
    CHECK(ctx, summary_text.find("run_id,timestamp") != std::string::npos);
    CHECK(ctx, summary_text.find("fake-1") != std::string::npos);
    CHECK(ctx, summary_text.find("Completed") != std::string::npos);

    const std::string detail_text = read_file(detail);
    CHECK(ctx, detail_text.find("run_id,elapsed_ms") != std::string::npos);
    CHECK(ctx, detail_text.find("fake-1") != std::string::npos);

    std::error_code remove_error;
    std::filesystem::remove_all(dir, remove_error);
}

}  // namespace

Suite fake_controller_suite() {
    return {
        {"validate_uses_guardrails", test_validate_uses_guardrails},
        {"start_rejects_invalid", test_start_rejects_invalid},
        {"start_is_nonblocking_and_completes", test_start_is_nonblocking_and_completes},
        {"stop_resolves_quickly", test_stop_resolves_quickly},
        {"start_while_running_is_rejected", test_start_while_running_is_rejected},
        {"shared_counter_reports_losses", test_shared_counter_reports_losses},
        {"memory_pressure_limit_hit", test_memory_pressure_limit_hit},
        {"export_csv_writes_files", test_export_csv_writes_files},
    };
}

}  // namespace memtest::test

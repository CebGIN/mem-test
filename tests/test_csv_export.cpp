#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "TestHarness.hpp"
#include "core/CsvExporter.hpp"
#include "core/ResultStore.hpp"

using namespace memtest;

namespace {

constexpr std::uint64_t kMiB = 1024 * 1024;

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

ExperimentResult make_result() {
    ExperimentResult result;
    result.run_id = "run-1";
    result.config.workload = WorkloadKind::SharedCounter;
    result.config.worker_mode = WorkerMode::RuntimeTasks;
    result.config.load_mode = LoadMode::TotalWork;
    result.config.force_swap = true;
    result.config.sync_mode = SyncMode::Mutex;
    result.config.threads = 4;
    result.config.increments_per_thread = 200000;
    result.config.memory_limit_kind = MemoryLimitKind::AddressSpace;
    result.config.memory_limit_bytes = 64 * kMiB;
    result.config.cpu_limit.seconds = 2;
    result.config.cpu_limit.quota_percent = 50;
    result.config.duration_ms = 2500;
    result.system_baseline.mem_total = 8ull * 1024ull * kMiB;
    result.system_baseline.mem_used = 3ull * 1024ull * kMiB;
    result.system_peak.mem_used = 4ull * 1024ull * kMiB;
    result.system_last.mem_used = 4ull * 1024ull * kMiB;
    result.system_cpu_peak_percent = 42.5;
    result.system_ram_delta = 1ull * 1024ull * kMiB;
    result.outcome = RunOutcome::MemoryLimitHit;
    result.detail = "fallo, con \"comillas\"\nsegunda linea";
    result.duration = std::chrono::milliseconds(1234);
    result.expected_value = 800000;
    result.observed_value = 799000;
    result.limits.memory_kind = MemoryLimitKind::AddressSpace;
    result.limits.memory_status = LimitStatus::Applied;
    result.limits.memory_applied = 64 * kMiB;
    result.limits.cpu_seconds_status = LimitStatus::Applied;
    result.limits.cpu_seconds_applied = 2;
    result.limits.cpu_quota_status = LimitStatus::Applied;
    result.limits.cpu_quota_applied = 50;
    result.limits.nr_throttled = 12;
    result.limits.throttled_usec = 3456;

    result.peak.vm_size = 80 * kMiB;
    result.peak.vm_rss = 60 * kMiB;
    result.peak.vm_hwm = 62 * kMiB;
    result.peak.minor_faults = 1234;
    result.peak.major_faults = 2;
    result.peak.cpu_user_s = 1.5;
    result.peak.cpu_sys_s = 0.25;
    result.peak.threads = 4;
    result.peak.voluntary_ctxt = 100;
    result.peak.involuntary_ctxt = 7;

    MetricsSnapshot first;
    first.elapsed = std::chrono::milliseconds(250);
    first.vm_rss = 30 * kMiB;
    first.vm_size = 70 * kMiB;
    first.minor_faults = 100;
    first.cpu_user_s = 0.5;
    first.cpu_sys_s = 0.1;
    first.threads = 4;
    first.load_1m = 1.25;

    MetricsSnapshot second = first;
    second.elapsed = std::chrono::milliseconds(500);
    second.vm_rss = 60 * kMiB;
    second.vm_size = 80 * kMiB;
    second.minor_faults = 1234;
    second.cpu_user_s = 1.5;
    second.cpu_sys_s = 0.25;

    result.series = {first, second};
    return result;
}

void test_result_store() {
    ResultStore store;
    CHECK(store.empty());
    CHECK_EQ(store.size(), std::size_t{0});

    ExperimentResult first = make_result();
    store.add(first);
    ExperimentResult second = first;
    second.run_id = "run-2";
    store.add(std::move(second));

    CHECK_EQ(store.size(), std::size_t{2});
    CHECK(store.find("run-1") != nullptr);
    CHECK(store.find("run-2") != nullptr);
    CHECK(store.find("run-3") == nullptr);

    const std::vector<ExperimentResult> snapshot = store.snapshot();
    CHECK_EQ(snapshot.size(), std::size_t{2});
    CHECK_EQ(snapshot.front().run_id, std::string("run-1"));

    store.clear();
    CHECK(store.empty());
}

void test_export_writes_summary_and_detail() {
    const std::filesystem::path dir = temp_dir("csv") / "anidado";
    const std::vector<ExperimentResult> results{make_result()};

    CsvExportPaths paths;
    std::string error;
    CHECK(CsvExporter::write(results, dir, &paths, &error));
    CHECK(error.empty());
    CHECK(!paths.summary.empty());
    CHECK_EQ(paths.details.size(), std::size_t{1});
    CHECK(std::filesystem::is_directory(dir));
    CHECK(std::filesystem::exists(paths.summary));
    CHECK(std::filesystem::exists(paths.details.front()));

    const std::string summary = read_file(paths.summary);
    CHECK(summary.find("run_id,timestamp,workload") != std::string::npos);
    CHECK(summary.find("run-1") != std::string::npos);
    CHECK(summary.find("MemoryLimitHit") != std::string::npos);
    CHECK(summary.find("Applied") != std::string::npos);
    CHECK(summary.find("1234") != std::string::npos);
    CHECK(summary.find("sys_mem_used_peak") != std::string::npos);
    CHECK(summary.find("TotalWork") != std::string::npos);
    CHECK(summary.find("nr_throttled") != std::string::npos);
    CHECK(summary.find("throttled_usec") != std::string::npos);
    CHECK(summary.find("42.5") != std::string::npos);
    CHECK(summary.find("2500") != std::string::npos);
    // El detalle va entre comillas, con las comillas internas duplicadas y el
    // salto de línea conservado dentro del campo.
    CHECK(summary.find("\"fallo, con \"\"comillas\"\"\nsegunda linea\"") !=
          std::string::npos);

    const std::string detail = read_file(paths.details.front());
    CHECK(detail.find("run_id,elapsed_ms,vm_size") != std::string::npos);
    CHECK_EQ(std::count(detail.begin(), detail.end(), '\n'), std::ptrdiff_t{3});
    CHECK(detail.find("500,") != std::string::npos);
    CHECK(detail.find("1.500") != std::string::npos);
    CHECK(detail.find("1.250") != std::string::npos);

    // UTF-8 sin BOM.
    if (summary.size() >= 3) {
        CHECK(!(static_cast<unsigned char>(summary[0]) == 0xEF &&
                static_cast<unsigned char>(summary[1]) == 0xBB &&
                static_cast<unsigned char>(summary[2]) == 0xBF));
    }

    std::error_code remove_error;
    std::filesystem::remove_all(dir.parent_path(), remove_error);
}

void test_export_empty_is_an_error() {
    const std::filesystem::path dir = temp_dir("csv-empty");
    CsvExportPaths paths;
    std::string error;
    CHECK(!CsvExporter::write({}, dir, &paths, &error));
    CHECK(!error.empty());
    CHECK(!std::filesystem::exists(dir));
}

}  // namespace

int main() {
    test_result_store();
    test_export_writes_summary_and_detail();
    test_export_empty_is_an_error();
    return TEST_RESULT();
}

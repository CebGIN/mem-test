#include "app/FakeExperimentController.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <utility>

#include "core/CsvExporter.hpp"
#include "core/SystemInfo.hpp"

namespace memtest {
namespace {

constexpr std::uint64_t kMiB = 1024ull * 1024ull;
constexpr std::uint64_t kPage = 4096;

std::uint64_t saturating_mul(std::uint64_t a, std::uint64_t b) {
    if (a != 0 && b > UINT64_MAX / a) {
        return UINT64_MAX;
    }
    return a * b;
}

void accumulate_peak(MetricsSnapshot& peak, const MetricsSnapshot& sample) {
    peak.elapsed = std::max(peak.elapsed, sample.elapsed);
    peak.vm_size = std::max(peak.vm_size, sample.vm_size);
    peak.vm_rss = std::max(peak.vm_rss, sample.vm_rss);
    peak.vm_hwm = std::max(peak.vm_hwm, sample.vm_hwm);
    peak.vm_swap = std::max(peak.vm_swap, sample.vm_swap);
    peak.rss_anon = std::max(peak.rss_anon, sample.rss_anon);
    peak.rss_file = std::max(peak.rss_file, sample.rss_file);
    peak.rss_shmem = std::max(peak.rss_shmem, sample.rss_shmem);
    peak.minor_faults = std::max(peak.minor_faults, sample.minor_faults);
    peak.major_faults = std::max(peak.major_faults, sample.major_faults);
    peak.cpu_user_s = std::max(peak.cpu_user_s, sample.cpu_user_s);
    peak.cpu_sys_s = std::max(peak.cpu_sys_s, sample.cpu_sys_s);
    peak.threads = std::max(peak.threads, sample.threads);
    peak.voluntary_ctxt = std::max(peak.voluntary_ctxt, sample.voluntary_ctxt);
    peak.involuntary_ctxt = std::max(peak.involuntary_ctxt, sample.involuntary_ctxt);
    peak.sys_swap_total = std::max(peak.sys_swap_total, sample.sys_swap_total);
    peak.sys_swap_free = std::max(peak.sys_swap_free, sample.sys_swap_free);
    peak.load_1m = std::max(peak.load_1m, sample.load_1m);
}

}  // namespace

FakeExperimentController::FakeExperimentController()
    : FakeExperimentController(read_system_info()) {}

FakeExperimentController::FakeExperimentController(SystemInfo info)
    : system_(std::move(info)), limits_(guardrails_from(system_)) {}

FakeExperimentController::~FakeExperimentController() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (runner_.joinable()) {
        runner_.join();
    }
}

ValidationReport FakeExperimentController::validate(const ExperimentConfig& config) const {
    return memtest::validate(config, limits_);
}

bool FakeExperimentController::start(const ExperimentConfig& config, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_run_ != 0) {
            if (error != nullptr) {
                *error = "Ya hay una ejecución en curso.";
            }
            return false;
        }
    }
    if (runner_.joinable()) {
        runner_.join();
    }

    const ValidationReport report = memtest::validate(config, limits_);
    if (!report.ok()) {
        if (error != nullptr) {
            *error = report.issues.front().message;
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    live_.state = RunState::Starting;
    live_.message = "Preparando ejecución simulada";
    live_.last = MetricsSnapshot{};
    live_.recent.clear();
    live_.system_baseline = make_system_sample(false);
    live_.system_peak = live_.system_baseline;
    live_.has_system_baseline = true;
    stop_requested_ = false;
    running_run_ = next_run_number_++;

    try {
        runner_ = std::thread(&FakeExperimentController::run, this, config, running_run_);
    } catch (const std::exception& exception) {
        running_run_ = 0;
        live_.state = RunState::Idle;
        live_.message = "No se pudo iniciar la simulación";
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
    return true;
}

void FakeExperimentController::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_run_ == 0) {
            return;
        }
        stop_requested_ = true;
        live_.state = RunState::Stopping;
        live_.message = "Deteniendo ejecución simulada";
    }
    cv_.notify_all();
}

LiveState FakeExperimentController::live() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool running = running_run_ != 0;
    live_.system = make_system_sample(running);
    live_.system_recent.push_back(live_.system);
    if (live_.system_recent.size() > 240) {
        live_.system_recent.erase(live_.system_recent.begin());
    }
    if (running) {
        live_.system_peak = live_.system;
    }
    return live_;
}

SystemSnapshot FakeExperimentController::make_system_sample(bool running) {
    SystemSnapshot sample;
    sample.elapsed = std::chrono::milliseconds(system_tick_ * 250);
    sample.mem_total = system_.total_ram;
    sample.mem_available = system_.available_ram > 0
                               ? system_.available_ram
                               : system_.total_ram / 2;
    sample.mem_used = sample.mem_total > sample.mem_available
                          ? sample.mem_total - sample.mem_available
                          : 0;
    if (running && sample.mem_total > 0) {
        sample.mem_used = std::min(sample.mem_total,
                                   sample.mem_used + sample.mem_total / 10);
    }
    sample.mem_free = sample.mem_available / 2;
    sample.swap_total = system_.swap_total;
    sample.swap_free = system_.swap_total;
    sample.swap_used = 0;
    sample.swap_available = system_.swap_total > 0;
    sample.cpu_total_percent = running ? 75.0 : 5.0;
    sample.cpu_user_percent = running ? 60.0 : 3.0;
    sample.cpu_system_percent = running ? 15.0 : 2.0;
    sample.load_1m = running ? 3.0 : 0.2;
    sample.load_5m = running ? 2.0 : 0.2;
    sample.load_15m = running ? 1.0 : 0.2;
    sample.procs_running = running ? 4 : 1;
    sample.procs_blocked = 0;
    sample.processes = 100 + system_tick_;
    sample.ctxt = system_tick_ * 1000;
    sample.ctxt_per_second = running ? 8000 : 500;
    sample.minor_faults = system_tick_ * (running ? 5000 : 100);
    sample.major_faults = system_tick_ / 10;
    sample.minor_faults_per_second = running ? 5000 : 100;
    sample.major_faults_per_second = running ? 5 : 0;
    ++system_tick_;
    return sample;
}

std::vector<ExperimentResult> FakeExperimentController::take_results() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ExperimentResult> results(pending_.begin(), pending_.end());
    pending_.clear();
    return results;
}

std::vector<ExperimentResult> FakeExperimentController::history() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.snapshot();
}

bool FakeExperimentController::export_csv(const std::filesystem::path& dir,
                                          std::string* error) {
    std::vector<ExperimentResult> results;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results = history_.snapshot();
    }
    return CsvExporter::write(results, dir, nullptr, error);
}

bool FakeExperimentController::wait_for(std::chrono::milliseconds period) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, period, [this] { return stop_requested_; });
    return stop_requested_;
}

MetricsSnapshot FakeExperimentController::make_sample(const ExperimentConfig& config,
                                                      std::size_t index,
                                                      std::size_t samples) const {
    const double fraction = samples > 0 ? static_cast<double>(index + 1) /
                                             static_cast<double>(samples)
                                       : 1.0;
    const int period_ms = std::max(1, config.wall_timeout_ms / static_cast<int>(samples));

    std::uint64_t mapped = config.memory_bytes;
    if (config.memory_limit_kind != MemoryLimitKind::None && config.memory_limit_bytes > 0) {
        mapped = std::min(mapped, config.memory_limit_bytes);
    }

    MetricsSnapshot sample;
    sample.elapsed = std::chrono::milliseconds(static_cast<long long>(index) * period_ms);
    sample.vm_size = 16 * kMiB + mapped;
    sample.vm_rss = 8 * kMiB + static_cast<std::uint64_t>(static_cast<double>(mapped) * fraction);
    sample.vm_hwm = sample.vm_rss;
    sample.rss_anon = sample.vm_rss;
    sample.rss_file = 2 * kMiB;
    sample.rss_shmem = 0;
    sample.minor_faults =
        static_cast<std::uint64_t>(static_cast<double>(mapped / kPage) * fraction) +
        static_cast<std::uint64_t>(64 * std::max(1, config.threads) * (index + 1));
    sample.major_faults = static_cast<std::uint64_t>(index / 4);

    const double elapsed_s = static_cast<double>(sample.elapsed.count()) / 1000.0;
    const double cpu_factor =
        config.workload == WorkloadKind::MemoryPressure ? 0.05 : 0.9;
    sample.cpu_user_s = elapsed_s * static_cast<double>(std::max(1, config.threads)) * cpu_factor;
    sample.cpu_sys_s = sample.cpu_user_s * 0.1;

    sample.threads = config.workload == WorkloadKind::MemoryPressure
                         ? 1
                         : std::max(1, config.threads);
    sample.voluntary_ctxt = static_cast<std::uint64_t>(100 * (index + 1));
    sample.involuntary_ctxt = static_cast<std::uint64_t>(5 * (index + 1));
    sample.sys_swap_total = system_.swap_total;
    sample.sys_swap_free = system_.swap_total;
    return sample;
}

void FakeExperimentController::run(ExperimentConfig config, std::uint64_t run_number) {
    const int samples = std::max(1, config.wall_timeout_ms / 250);
    const auto period = std::chrono::milliseconds(std::max(1, config.wall_timeout_ms / samples));

    std::vector<MetricsSnapshot> series;
    series.reserve(static_cast<std::size_t>(samples));
    MetricsSnapshot peak;
    bool stopped = wait_for(std::chrono::milliseconds(30));

    if (!stopped) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            live_.state = RunState::Running;
            live_.message = "Ejecución simulada en curso";
        }
        for (int i = 0; i < samples; ++i) {
            const MetricsSnapshot sample = make_sample(
                config, static_cast<std::size_t>(i), static_cast<std::size_t>(samples));
            series.push_back(sample);
            accumulate_peak(peak, sample);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                live_.last = sample;
                live_.recent.push_back(sample);
                if (live_.recent.size() > 120) {
                    live_.recent.erase(live_.recent.begin());
                }
            }
            if (wait_for(period)) {
                stopped = true;
                break;
            }
        }
    }

    if (series.empty()) {
        const MetricsSnapshot sample = make_sample(config, 0, static_cast<std::size_t>(samples));
        series.push_back(sample);
        accumulate_peak(peak, sample);
    }

    const bool memory_hit =
        config.workload == WorkloadKind::MemoryPressure &&
        config.memory_limit_kind != MemoryLimitKind::None &&
        config.memory_limit_bytes > 0 && config.memory_bytes >= config.memory_limit_bytes;

    ExperimentResult result;
    result.run_id = "fake-" + std::to_string(run_number);
    result.config = config;

    result.limits.memory_kind = config.memory_limit_kind;
    if (config.memory_limit_kind == MemoryLimitKind::None) {
        result.limits.memory_status = LimitStatus::NotRequested;
    } else if (config.memory_limit_bytes > 0) {
        result.limits.memory_status = LimitStatus::Applied;
        result.limits.memory_applied = config.memory_limit_bytes;
    } else {
        result.limits.memory_status = LimitStatus::NotApplied;
    }
    if (config.cpu_limit.seconds.has_value()) {
        result.limits.cpu_seconds_status = LimitStatus::Applied;
        result.limits.cpu_seconds_applied = *config.cpu_limit.seconds;
    }
    if (config.cpu_limit.quota_percent.has_value()) {
        // El controlador fake no crea cgroups: simula la cuota como aplicada.
        result.limits.cpu_quota_status = LimitStatus::Applied;
        result.limits.cpu_quota_applied = *config.cpu_limit.quota_percent;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped = stopped || stop_requested_;
    }

    if (stopped) {
        result.outcome = RunOutcome::Stopped;
        result.detail = "Detenida por el usuario (simulación).";
        result.exit_code = 12;
    } else if (memory_hit) {
        result.outcome = RunOutcome::MemoryLimitHit;
        result.detail = "Límite de memoria alcanzado (simulación).";
        result.exit_code = 10;
    } else {
        result.outcome = RunOutcome::Completed;
        result.detail = "Ejecución simulada completada.";
        result.exit_code = 0;
    }

    result.peak = peak;
    result.last = series.back();
    result.series = std::move(series);
    result.duration = result.last.elapsed;
    if (result.outcome == RunOutcome::Completed) {
        result.duration += period;
    }

    if (config.workload == WorkloadKind::SharedCounter) {
        result.expected_value = saturating_mul(
            static_cast<std::uint64_t>(std::max(1, config.threads)),
            config.increments_per_thread);
        if (stopped) {
            result.observed_value = result.expected_value / 2;
        } else if (config.sync_mode == SyncMode::None) {
            result.observed_value = result.expected_value - result.expected_value / 20;
        } else {
            result.observed_value = result.expected_value;
        }
        if (config.sync_mode == SyncMode::None && !stopped) {
            result.detail = "Actualizaciones perdidas: " +
                            std::to_string(result.expected_value - result.observed_value) +
                            " de " + std::to_string(result.expected_value) + " (simulación).";
        }
    } else if (config.workload == WorkloadKind::MemoryPressure) {
        const std::uint64_t stride = config.page_stride_bytes > 0 ? config.page_stride_bytes : 1;
        result.expected_value = config.memory_bytes / stride;
        result.observed_value = (memory_hit || stopped) ? result.expected_value / 2
                                                        : result.expected_value;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        result.system_baseline = live_.system_baseline;
        result.system_peak = live_.system;
        result.system_last = live_.system;
        result.system_ram_delta =
            result.system_peak.mem_used > result.system_baseline.mem_used
                ? result.system_peak.mem_used - result.system_baseline.mem_used
                : 0;
        result.system_swap_delta =
            result.system_peak.swap_used > result.system_baseline.swap_used
                ? result.system_peak.swap_used - result.system_baseline.swap_used
                : 0;
        result.system_cpu_peak_percent = result.system_peak.cpu_total_percent;
        result.system_minor_faults_delta =
            result.system_peak.minor_faults > result.system_baseline.minor_faults
                ? result.system_peak.minor_faults -
                      result.system_baseline.minor_faults
                : 0;
        result.system_major_faults_delta =
            result.system_peak.major_faults > result.system_baseline.major_faults
                ? result.system_peak.major_faults -
                      result.system_baseline.major_faults
                : 0;
        pending_.push_back(result);
        live_.state = RunState::Done;
        live_.message = result.detail;
        history_.add(std::move(result));
        running_run_ = 0;
        stop_requested_ = false;
    }
}

}  // namespace memtest

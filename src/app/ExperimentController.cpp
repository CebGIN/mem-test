#include "app/ExperimentController.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

#include "app/ExperimentRunner.hpp"
#include "core/CsvExporter.hpp"
#include "limits/CgroupCpuQuota.hpp"
#include "metrics/IMetricsSource.hpp"
#include "process/IProcessSupervisor.hpp"

namespace memtest {
namespace {

std::string first_error(const ValidationReport& report) {
    if (report.issues.empty()) {
        return "configuracion invalida";
    }
    return report.issues.front().message;
}

}  // namespace

ExperimentController::ExperimentController(
    std::unique_ptr<IMetricsSource> metrics,
    std::unique_ptr<IProcessSupervisor> supervisor, SystemInfo info)
    : metrics_(std::move(metrics)), supervisor_(std::move(supervisor)) {
    if (info.total_ram == 0 && metrics_) {
        info = metrics_->system_info();
    }
    system_ = info;
    limits_ = guardrails_from(system_);
    monitor_start_ = std::chrono::steady_clock::now();
}

ExperimentController::~ExperimentController() {
    stop();
    if (runner_.joinable()) {
        runner_.join();
    }
}

ValidationReport ExperimentController::validate(
    const ExperimentConfig& config) const {
    return memtest::validate(config, limits_);
}

bool ExperimentController::start(const ExperimentConfig& config,
                                 std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (!metrics_ || !supervisor_) {
        if (error != nullptr) {
            *error = "controlador sin fuente de metricas o supervisor";
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
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
            *error = first_error(report);
        }
        return false;
    }

    if (config.cpu_limit.quota_percent.has_value()) {
        std::string quota_detail;
        if (!limits::CgroupCpuQuota::supported(&quota_detail)) {
            if (error != nullptr) {
                *error = "Cuota de CPU no soportada en este entorno; usá "
                         "RLIMIT_CPU.";
                if (!quota_detail.empty()) {
                    *error += " " + quota_detail;
                }
            }
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            if (error != nullptr) {
                *error = "Ya hay una ejecución en curso.";
            }
            return false;
        }

        token_ = std::make_unique<CancellationToken>();
        stop_requested_ = false;
        live_.state = RunState::Starting;
        live_.message = "Preparando ejecución";
        live_.last = MetricsSnapshot{};
        live_.recent.clear();
        live_.system_baseline = SystemSnapshot{};
        live_.system_peak = SystemSnapshot{};
        live_.has_system_baseline = false;
        run_system_samples_.clear();
        // La línea base previa al spawn sirve de respaldo; en READY se
        // reemplaza por una muestra sin el costo de arranque del worker.
        sample_system_locked(true);
        if (!live_.system_recent.empty()) {
            live_.system_baseline = live_.system;
            live_.has_system_baseline = true;
        }

        try {
            running_ = true;
            runner_ = std::thread(&ExperimentController::run, this, config);
        } catch (const std::exception& exception) {
            running_ = false;
            live_.state = RunState::Idle;
            live_.message = "No se pudo iniciar la ejecución";
            if (error != nullptr) {
                *error = exception.what();
            }
            return false;
        }
    }
    return true;
}

void ExperimentController::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return;
    }
    stop_requested_ = true;
    if (token_) {
        token_->cancel();
    }
    live_.state = RunState::Stopping;
    live_.message = "Deteniendo ejecución";
}

LiveState ExperimentController::live() {
    std::lock_guard<std::mutex> lock(mutex_);
    sample_system_locked();
    return live_;
}

void ExperimentController::sample_system_locked(bool force) {
    if (!metrics_) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && system_sampled_ &&
        now - last_system_sample_ < std::chrono::milliseconds(500)) {
        return;
    }

    SystemSnapshot snapshot;
    if (!metrics_->sample_system(snapshot)) {
        return;
    }
    snapshot.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - monitor_start_);

    live_.system = snapshot;
    live_.system_recent.push_back(snapshot);
    if (live_.system_recent.size() > 240) {
        live_.system_recent.erase(live_.system_recent.begin());
    }
    if (running_) {
        run_system_samples_.push_back(snapshot);
    }
    last_system_sample_ = now;
    system_sampled_ = true;
}

void ExperimentController::update_system_impact_locked(ExperimentResult& result) {
    SystemSnapshot baseline;
    if (live_.has_system_baseline) {
        baseline = live_.system_baseline;
    } else if (!run_system_samples_.empty()) {
        baseline = run_system_samples_.front();
    } else {
        baseline = live_.system;
    }

    SystemSnapshot peak = baseline;
    SystemSnapshot last = baseline;
    for (const SystemSnapshot& sample : run_system_samples_) {
        peak.mem_used = std::max(peak.mem_used, sample.mem_used);
        peak.swap_used = std::max(peak.swap_used, sample.swap_used);
        peak.swap_free = sample.swap_free;
        peak.cpu_total_percent =
            std::max(peak.cpu_total_percent, sample.cpu_total_percent);
        peak.load_1m = std::max(peak.load_1m, sample.load_1m);
        peak.load_5m = std::max(peak.load_5m, sample.load_5m);
        peak.load_15m = std::max(peak.load_15m, sample.load_15m);
        peak.ctxt = std::max(peak.ctxt, sample.ctxt);
        peak.ctxt_per_second =
            std::max(peak.ctxt_per_second, sample.ctxt_per_second);
        peak.minor_faults = std::max(peak.minor_faults, sample.minor_faults);
        peak.major_faults = std::max(peak.major_faults, sample.major_faults);
        peak.minor_faults_per_second = std::max(
            peak.minor_faults_per_second, sample.minor_faults_per_second);
        peak.major_faults_per_second = std::max(
            peak.major_faults_per_second, sample.major_faults_per_second);
        peak.swap_in = std::max(peak.swap_in, sample.swap_in);
        peak.swap_out = std::max(peak.swap_out, sample.swap_out);
        last = sample;
    }

    result.system_baseline = baseline;
    result.system_peak = peak;
    result.system_last = last;
    result.system_ram_delta =
        peak.mem_used > baseline.mem_used ? peak.mem_used - baseline.mem_used : 0;
    result.system_swap_delta =
        peak.swap_used > baseline.swap_used ? peak.swap_used - baseline.swap_used
                                            : 0;
    result.system_cpu_peak_percent = peak.cpu_total_percent;
    result.system_minor_faults_delta =
        peak.minor_faults > baseline.minor_faults
            ? peak.minor_faults - baseline.minor_faults
            : 0;
    result.system_major_faults_delta =
        peak.major_faults > baseline.major_faults
            ? peak.major_faults - baseline.major_faults
            : 0;

    live_.system_baseline = baseline;
    live_.system_peak = peak;
    live_.has_system_baseline = true;
}

std::vector<ExperimentResult> ExperimentController::take_results() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ExperimentResult> results(pending_.begin(), pending_.end());
    pending_.clear();
    return results;
}

std::vector<ExperimentResult> ExperimentController::history() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.snapshot();
}

bool ExperimentController::export_csv(const std::filesystem::path& dir,
                                      std::string* error) {
    std::vector<ExperimentResult> results;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results = history_.snapshot();
    }
    return CsvExporter::write(results, dir, nullptr, error);
}

void ExperimentController::run(ExperimentConfig config) {
    ExperimentResult result;
    result.config = config;

    try {
        ExperimentRunner runner(*metrics_, *supervisor_);
        result = runner.run(
            config, *token_,
            [this](const MetricsSnapshot& sample) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stop_requested_) {
                    live_.state = RunState::Running;
                }
                live_.last = sample;
                live_.recent.push_back(sample);
                if (live_.recent.size() > 120) {
                    live_.recent.erase(live_.recent.begin());
                }
            },
            [this](const WorkerEvent& event) {
                if (event.type == WorkerEvent::Type::Ready) {
                    // Línea base del sistema sin el costo de arranque del worker.
                    SystemSnapshot ready;
                    if (metrics_ && metrics_->sample_system(ready)) {
                        ready.elapsed =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - monitor_start_);
                        std::lock_guard<std::mutex> lock(mutex_);
                        live_.system = ready;
                        live_.system_baseline = ready;
                        live_.has_system_baseline = true;
                        live_.message = "Ejecutando carga";
                        if (running_) {
                            run_system_samples_.push_back(ready);
                        }
                        return;
                    }
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (event.type == WorkerEvent::Type::Ready) {
                    live_.message = "Ejecutando carga";
                } else if (event.type == WorkerEvent::Type::Error) {
                    const auto detail = event.fields.find("detail");
                    if (detail != event.fields.end()) {
                        live_.message = detail->second;
                    }
                }
            });
    } catch (const std::exception& exception) {
        result.outcome = RunOutcome::Error;
        result.detail = exception.what();
        result.exit_code = 13;
    } catch (...) {
        result.outcome = RunOutcome::Error;
        result.detail = "excepción desconocida en el runner";
        result.exit_code = 13;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    update_system_impact_locked(result);
    pending_.push_back(result);
    live_.state = RunState::Done;
    live_.last = result.last;
    if (!result.detail.empty()) {
        live_.message = result.detail;
    } else {
        live_.message = "Ejecución terminada";
    }
    history_.add(std::move(result));
    running_ = false;
    stop_requested_ = false;
}

}  // namespace memtest

#include "app/ExperimentController.hpp"

#include <exception>
#include <utility>

#include "app/ExperimentRunner.hpp"
#include "core/CsvExporter.hpp"
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
        live_ = LiveState{};
        live_.state = RunState::Starting;
        live_.message = "Preparando ejecución";

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

LiveState ExperimentController::live() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return live_;
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

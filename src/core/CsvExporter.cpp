#include "core/CsvExporter.hpp"

#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

namespace memtest {
namespace {

std::string escape_field(const std::string& value) {
    const bool needs_quotes = value.find_first_of(",\"\r\n") != std::string::npos;
    if (!needs_quotes) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

std::string number(double value, int precision) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string optional_int(const std::optional<int>& value) {
    return value.has_value() ? std::to_string(*value) : std::string();
}

std::string workload_name(WorkloadKind kind) {
    switch (kind) {
        case WorkloadKind::Concurrency: return "Concurrency";
        case WorkloadKind::CpuLimit: return "CpuLimit";
        case WorkloadKind::MemoryPressure: return "MemoryPressure";
        case WorkloadKind::SharedCounter: return "SharedCounter";
    }
    return "Unknown";
}

std::string worker_mode_name(WorkerMode mode) {
    switch (mode) {
        case WorkerMode::NativeThreads: return "NativeThreads";
        case WorkerMode::RuntimeTasks: return "RuntimeTasks";
    }
    return "Unknown";
}

std::string load_mode_name(LoadMode mode) {
    switch (mode) {
        case LoadMode::Duration: return "Duration";
        case LoadMode::TotalWork: return "TotalWork";
    }
    return "Unknown";
}

std::string sync_mode_name(SyncMode mode) {
    switch (mode) {
        case SyncMode::None: return "None";
        case SyncMode::Mutex: return "Mutex";
        case SyncMode::Atomic: return "Atomic";
    }
    return "Unknown";
}

std::string memory_kind_name(MemoryLimitKind kind) {
    switch (kind) {
        case MemoryLimitKind::None: return "None";
        case MemoryLimitKind::AddressSpace: return "AddressSpace";
        case MemoryLimitKind::DataSegment: return "DataSegment";
    }
    return "Unknown";
}

std::string outcome_name(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Completed: return "Completed";
        case RunOutcome::Stopped: return "Stopped";
        case RunOutcome::MemoryLimitHit: return "MemoryLimitHit";
        case RunOutcome::CpuLimitHit: return "CpuLimitHit";
        case RunOutcome::ExternalKill: return "ExternalKill";
        case RunOutcome::Error: return "Error";
    }
    return "Unknown";
}

std::string limit_status_name(LimitStatus status) {
    switch (status) {
        case LimitStatus::NotRequested: return "NotRequested";
        case LimitStatus::Applied: return "Applied";
        case LimitStatus::NotApplied: return "NotApplied";
        case LimitStatus::Unsupported: return "Unsupported";
    }
    return "Unknown";
}

std::string export_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) == nullptr) {
        return "00000000-000000";
    }
    char buffer[32] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local) == 0) {
        return "00000000-000000";
    }
    return buffer;
}

std::string safe_file_part(const std::string& run_id) {
    std::string safe;
    safe.reserve(run_id.size());
    for (const char character : run_id) {
        const unsigned char byte = static_cast<unsigned char>(character);
        safe.push_back(std::isalnum(byte) != 0 || character == '-' || character == '_'
                           ? character
                           : '_');
    }
    return safe.empty() ? "run" : safe;
}

class CsvRow {
public:
    CsvRow& add(const std::string& value) {
        if (!first_) {
            out_ << ',';
        }
        first_ = false;
        out_ << escape_field(value);
        return *this;
    }

    CsvRow& add(std::uint64_t value) { return add(std::to_string(value)); }
    CsvRow& add(int value) { return add(std::to_string(value)); }

    std::string str() const { return out_.str(); }

private:
    std::ostringstream out_;
    bool first_ = true;
};

bool write_summary(const std::vector<ExperimentResult>& results,
                   const std::filesystem::path& path,
                   std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) {
            *error = "No se pudo crear " + path.string();
        }
        return false;
    }

    out << "run_id,timestamp,workload,worker_mode,load_mode,sync_mode,threads,"
           "work_units,increments,memory_bytes,page_stride,force_swap,"
           "memory_limit_kind,memory_limit_bytes,cpu_seconds,cpu_quota,"
           "limit_cpu_quota_status,nr_throttled,throttled_usec,"
           "outcome,duration_ms,vm_size_peak,vm_rss_peak,vm_hwm,"
           "vm_swap_peak,minor_faults,major_faults,cpu_user_s,cpu_sys_s,threads_peak,"
           "voluntary_ctxt,involuntary_ctxt,expected,observed,limit_memory_status,"
           "limit_cpu_status,detail,run_duration_ms,sys_mem_total,sys_mem_used_base,"
           "sys_mem_used_peak,sys_mem_used_last,sys_swap_total,sys_swap_used_base,"
           "sys_swap_used_peak,sys_swap_used_last,sys_cpu_peak_pct,sys_load1_peak,"
           "sys_minor_faults_delta,sys_major_faults_delta,sys_ram_delta,"
           "sys_swap_delta\n";

    const std::string timestamp = export_timestamp();
    for (const ExperimentResult& result : results) {
        CsvRow row;
        row.add(result.run_id)
            .add(timestamp)
            .add(workload_name(result.config.workload))
            .add(worker_mode_name(result.config.worker_mode))
            .add(load_mode_name(result.config.load_mode))
            .add(sync_mode_name(result.config.sync_mode))
            .add(result.config.threads)
            .add(result.config.work_units)
            .add(result.config.increments_per_thread)
            .add(result.config.memory_bytes)
            .add(result.config.page_stride_bytes)
            .add(result.config.force_swap ? 1 : 0)
            .add(memory_kind_name(result.config.memory_limit_kind))
            .add(result.config.memory_limit_bytes)
            .add(optional_int(result.config.cpu_limit.seconds))
            .add(optional_int(result.config.cpu_limit.quota_percent))
            .add(limit_status_name(result.limits.cpu_quota_status))
            .add(result.limits.nr_throttled)
            .add(result.limits.throttled_usec)
            .add(outcome_name(result.outcome))
            .add(std::to_string(result.duration.count()))
            .add(result.peak.vm_size)
            .add(result.peak.vm_rss)
            .add(result.peak.vm_hwm)
            .add(result.peak.vm_swap)
            .add(result.peak.minor_faults)
            .add(result.peak.major_faults)
            .add(number(result.peak.cpu_user_s, 3))
            .add(number(result.peak.cpu_sys_s, 3))
            .add(result.peak.threads)
            .add(result.peak.voluntary_ctxt)
            .add(result.peak.involuntary_ctxt)
            .add(result.expected_value)
            .add(result.observed_value)
            .add(limit_status_name(result.limits.memory_status))
            .add(limit_status_name(result.limits.cpu_seconds_status))
            .add(result.detail)
            .add(result.config.duration_ms)
            .add(result.system_baseline.mem_total)
            .add(result.system_baseline.mem_used)
            .add(result.system_peak.mem_used)
            .add(result.system_last.mem_used)
            .add(result.system_baseline.swap_total)
            .add(result.system_baseline.swap_used)
            .add(result.system_peak.swap_used)
            .add(result.system_last.swap_used)
            .add(number(result.system_cpu_peak_percent, 1))
            .add(number(result.system_peak.load_1m, 2))
            .add(result.system_minor_faults_delta)
            .add(result.system_major_faults_delta)
            .add(result.system_ram_delta)
            .add(result.system_swap_delta);
        out << row.str() << '\n';
    }

    if (!out) {
        if (error != nullptr) {
            *error = "No se pudo escribir " + path.string();
        }
        return false;
    }
    return true;
}

bool write_detail(const ExperimentResult& result,
                  const std::filesystem::path& path,
                  std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) {
            *error = "No se pudo crear " + path.string();
        }
        return false;
    }

    out << "run_id,elapsed_ms,vm_size,vm_rss,vm_hwm,vm_swap,rss_anon,rss_file,"
           "rss_shmem,minor_faults,major_faults,cpu_user_s,cpu_sys_s,threads,"
           "voluntary_ctxt,involuntary_ctxt,load_1m\n";

    for (const MetricsSnapshot& sample : result.series) {
        CsvRow row;
        row.add(result.run_id)
            .add(std::to_string(sample.elapsed.count()))
            .add(sample.vm_size)
            .add(sample.vm_rss)
            .add(sample.vm_hwm)
            .add(sample.vm_swap)
            .add(sample.rss_anon)
            .add(sample.rss_file)
            .add(sample.rss_shmem)
            .add(sample.minor_faults)
            .add(sample.major_faults)
            .add(number(sample.cpu_user_s, 3))
            .add(number(sample.cpu_sys_s, 3))
            .add(sample.threads)
            .add(sample.voluntary_ctxt)
            .add(sample.involuntary_ctxt)
            .add(number(sample.load_1m, 3));
        out << row.str() << '\n';
    }

    if (!out) {
        if (error != nullptr) {
            *error = "No se pudo escribir " + path.string();
        }
        return false;
    }
    return true;
}

}  // namespace

bool CsvExporter::write(const std::vector<ExperimentResult>& results,
                        const std::filesystem::path& directory,
                        CsvExportPaths* paths,
                        std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (paths != nullptr) {
        *paths = CsvExportPaths{};
    }
    if (results.empty()) {
        if (error != nullptr) {
            *error = "No hay ejecuciones para exportar.";
        }
        return false;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(directory, directory_error);
    std::error_code directory_check;
    const bool directory_ok = std::filesystem::is_directory(directory, directory_check);
    if ((directory_error || directory_check) && !directory_ok) {
        if (error != nullptr) {
            *error = "No se pudo crear el directorio " + directory.string() + ": " +
                     (directory_error ? directory_error.message()
                                      : directory_check.message());
        }
        return false;
    }

    const std::string timestamp = export_timestamp();
    std::filesystem::path summary = directory / ("resumen_" + timestamp + ".csv");
    for (int suffix = 1;; ++suffix) {
        std::error_code exists_error;
        if (!std::filesystem::exists(summary, exists_error) || exists_error) {
            break;
        }
        summary =
            directory / ("resumen_" + timestamp + "-" + std::to_string(suffix) + ".csv");
    }
    if (!write_summary(results, summary, error)) {
        return false;
    }
    if (paths != nullptr) {
        paths->summary = summary;
    }

    for (const ExperimentResult& result : results) {
        const std::filesystem::path detail =
            directory / ("detalle_" + safe_file_part(result.run_id) + ".csv");
        if (!write_detail(result, detail, error)) {
            return false;
        }
        if (paths != nullptr) {
            paths->details.push_back(detail);
        }
    }
    return true;
}

}  // namespace memtest

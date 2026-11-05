#include "process/WorkerProtocol.hpp"

#include <charconv>
#include <map>
#include <sstream>
#include <system_error>

namespace memtest {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

void set_error(std::string* error, const std::string& text) {
    if (error != nullptr) {
        *error = text;
    }
}

bool parse_u64(std::string_view text, std::uint64_t& out, std::string* error,
               const char* field) {
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, out);
    if (result.ec != std::errc{} || result.ptr != last) {
        set_error(error, std::string("valor invalido para ") + field);
        return false;
    }
    return true;
}

bool parse_int(std::string_view text, int& out, std::string* error,
               const char* field) {
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, out);
    if (result.ec != std::errc{} || result.ptr != last) {
        set_error(error, std::string("valor invalido para ") + field);
        return false;
    }
    return true;
}

bool parse_fields(std::string_view text,
                  std::map<std::string, std::string>& fields,
                  std::string* error) {
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() &&
               (text[index] == ' ' || text[index] == '\t')) {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }

        const std::size_t key_start = index;
        while (index < text.size() && text[index] != '=' &&
               text[index] != ' ' && text[index] != '\t') {
            ++index;
        }
        if (index >= text.size() || text[index] != '=') {
            set_error(error, "campo sin '=' en el protocolo");
            return false;
        }
        const std::string key(text.substr(key_start, index - key_start));
        if (key.empty()) {
            set_error(error, "campo vacio en el protocolo");
            return false;
        }
        ++index;  // '='

        std::string value;
        if (index < text.size() && text[index] == '"') {
            ++index;
            bool closed = false;
            while (index < text.size()) {
                const char c = text[index++];
                if (c == '\\' && index < text.size()) {
                    value.push_back(text[index++]);
                } else if (c == '"') {
                    closed = true;
                    break;
                } else {
                    value.push_back(c);
                }
            }
            if (!closed) {
                set_error(error, "comillas sin cerrar en el protocolo");
                return false;
            }
            if (index < text.size() && text[index] != ' ' &&
                text[index] != '\t') {
                set_error(error, "texto despues de comillas en el protocolo");
                return false;
            }
        } else {
            const std::size_t value_start = index;
            while (index < text.size() && text[index] != ' ' &&
                   text[index] != '\t') {
                ++index;
            }
            value.assign(text.substr(value_start, index - value_start));
        }
        fields[key] = std::move(value);
    }
    return true;
}

std::string quote_value(const std::string& value) {
    const bool needs_quotes =
        value.empty() || value.find_first_of(" \t\"=") != std::string::npos;
    if (!needs_quotes) {
        return value;
    }

    std::string out;
    out.push_back('"');
    for (char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string workload_name(WorkloadKind kind) {
    switch (kind) {
        case WorkloadKind::Concurrency: return "concurrency";
        case WorkloadKind::CpuLimit: return "cpu_limit";
        case WorkloadKind::MemoryPressure: return "memory_pressure";
        case WorkloadKind::SharedCounter: return "shared_counter";
    }
    return "concurrency";
}

std::optional<WorkloadKind> workload_from_name(std::string_view text) {
    if (text == "concurrency") return WorkloadKind::Concurrency;
    if (text == "cpu_limit" || text == "cpu") return WorkloadKind::CpuLimit;
    if (text == "memory_pressure" || text == "memory") {
        return WorkloadKind::MemoryPressure;
    }
    if (text == "shared_counter" || text == "counter") {
        return WorkloadKind::SharedCounter;
    }
    return std::nullopt;
}

std::string worker_mode_name(WorkerMode mode) {
    return mode == WorkerMode::NativeThreads ? "native" : "runtime";
}

std::optional<WorkerMode> worker_mode_from_name(std::string_view text) {
    if (text == "native" || text == "native_threads") {
        return WorkerMode::NativeThreads;
    }
    if (text == "runtime" || text == "runtime_tasks") {
        return WorkerMode::RuntimeTasks;
    }
    return std::nullopt;
}

std::string load_mode_name(LoadMode mode) {
    return mode == LoadMode::TotalWork ? "total" : "duration";
}

std::optional<LoadMode> load_mode_from_name(std::string_view text) {
    if (text == "duration" || text == "time") return LoadMode::Duration;
    if (text == "total" || text == "total_work") return LoadMode::TotalWork;
    return std::nullopt;
}

std::string sync_mode_name(SyncMode mode) {
    switch (mode) {
        case SyncMode::None: return "none";
        case SyncMode::Mutex: return "mutex";
        case SyncMode::Atomic: return "atomic";
    }
    return "none";
}

std::optional<SyncMode> sync_mode_from_name(std::string_view text) {
    if (text == "none") return SyncMode::None;
    if (text == "mutex") return SyncMode::Mutex;
    if (text == "atomic") return SyncMode::Atomic;
    return std::nullopt;
}

std::string memory_kind_name(MemoryLimitKind kind) {
    switch (kind) {
        case MemoryLimitKind::None: return "none";
        case MemoryLimitKind::AddressSpace: return "as";
        case MemoryLimitKind::DataSegment: return "data";
    }
    return "none";
}

std::optional<MemoryLimitKind> memory_kind_from_name(std::string_view text) {
    if (text == "none") return MemoryLimitKind::None;
    if (text == "as" || text == "address_space") {
        return MemoryLimitKind::AddressSpace;
    }
    if (text == "data" || text == "data_segment") {
        return MemoryLimitKind::DataSegment;
    }
    return std::nullopt;
}

std::optional<WorkerEvent::Type> event_type_from_name(std::string_view text) {
    if (text == "HELLO") return WorkerEvent::Type::Hello;
    if (text == "READY") return WorkerEvent::Type::Ready;
    if (text == "LIMITS") return WorkerEvent::Type::Limits;
    if (text == "PROGRESS") return WorkerEvent::Type::Progress;
    if (text == "RESULT") return WorkerEvent::Type::Result;
    if (text == "ERROR") return WorkerEvent::Type::Error;
    if (text == "END") return WorkerEvent::Type::End;
    return std::nullopt;
}

std::string_view event_type_name(WorkerEvent::Type type) {
    switch (type) {
        case WorkerEvent::Type::Hello: return "HELLO";
        case WorkerEvent::Type::Ready: return "READY";
        case WorkerEvent::Type::Limits: return "LIMITS";
        case WorkerEvent::Type::Progress: return "PROGRESS";
        case WorkerEvent::Type::Result: return "RESULT";
        case WorkerEvent::Type::Error: return "ERROR";
        case WorkerEvent::Type::End: return "END";
    }
    return "ERROR";
}

}  // namespace

std::string encode_config(const ExperimentConfig& config) {
    std::ostringstream out;
    out << "workload=" << workload_name(config.workload)
        << " worker_mode=" << worker_mode_name(config.worker_mode)
        << " load_mode=" << load_mode_name(config.load_mode)
        << " sync_mode=" << sync_mode_name(config.sync_mode)
        << " threads=" << config.threads
        << " work_units=" << config.work_units
        << " increments=" << config.increments_per_thread
        << " memory_bytes=" << config.memory_bytes
        << " page_stride=" << config.page_stride_bytes
        << " force_swap=" << (config.force_swap ? 1 : 0)
        << " wall_timeout_ms=" << config.wall_timeout_ms
        << " duration_ms=" << config.duration_ms
        << " memory_limit_kind=" << memory_kind_name(config.memory_limit_kind)
        << " memory_limit_bytes=" << config.memory_limit_bytes
        << " cpu_seconds=" << config.cpu_limit.seconds.value_or(0)
        << " cpu_quota=" << config.cpu_limit.quota_percent.value_or(0);
    return out.str();
}

bool parse_config(std::string_view line, ExperimentConfig& out,
                  std::string* error) {
    line = trim(line);
    if (line.empty()) {
        set_error(error, "configuracion vacia");
        return false;
    }
    if (line.size() > kMaxProtocolLine) {
        set_error(error, "configuracion demasiado larga");
        return false;
    }

    std::map<std::string, std::string> fields;
    if (!parse_fields(line, fields, error)) {
        return false;
    }

    ExperimentConfig config;
    for (const auto& [key, value] : fields) {
        if (key == "workload") {
            const auto parsed = workload_from_name(value);
            if (!parsed) {
                set_error(error, "workload invalido");
                return false;
            }
            config.workload = *parsed;
        } else if (key == "worker_mode") {
            const auto parsed = worker_mode_from_name(value);
            if (!parsed) {
                set_error(error, "worker_mode invalido");
                return false;
            }
            config.worker_mode = *parsed;
        } else if (key == "load_mode") {
            const auto parsed = load_mode_from_name(value);
            if (!parsed) {
                set_error(error, "load_mode invalido");
                return false;
            }
            config.load_mode = *parsed;
        } else if (key == "sync_mode") {
            const auto parsed = sync_mode_from_name(value);
            if (!parsed) {
                set_error(error, "sync_mode invalido");
                return false;
            }
            config.sync_mode = *parsed;
        } else if (key == "threads") {
            if (!parse_int(value, config.threads, error, "threads")) return false;
        } else if (key == "work_units") {
            if (!parse_u64(value, config.work_units, error, "work_units")) return false;
        } else if (key == "increments") {
            if (!parse_u64(value, config.increments_per_thread, error,
                           "increments")) return false;
        } else if (key == "memory_bytes") {
            if (!parse_u64(value, config.memory_bytes, error, "memory_bytes")) {
                return false;
            }
        } else if (key == "page_stride") {
            if (!parse_u64(value, config.page_stride_bytes, error,
                           "page_stride")) return false;
        } else if (key == "force_swap") {
            int enabled = 0;
            if (!parse_int(value, enabled, error, "force_swap")) return false;
            config.force_swap = enabled != 0;
        } else if (key == "wall_timeout_ms") {
            if (!parse_int(value, config.wall_timeout_ms, error,
                           "wall_timeout_ms")) return false;
        } else if (key == "duration_ms") {
            if (!parse_int(value, config.duration_ms, error, "duration_ms")) {
                return false;
            }
        } else if (key == "memory_limit_kind") {
            const auto parsed = memory_kind_from_name(value);
            if (!parsed) {
                set_error(error, "memory_limit_kind invalido");
                return false;
            }
            config.memory_limit_kind = *parsed;
        } else if (key == "memory_limit_bytes") {
            if (!parse_u64(value, config.memory_limit_bytes, error,
                           "memory_limit_bytes")) return false;
        } else if (key == "cpu_seconds") {
            int seconds = 0;
            if (!parse_int(value, seconds, error, "cpu_seconds")) return false;
            if (seconds > 0) {
                config.cpu_limit.seconds = seconds;
            }
        } else if (key == "cpu_quota") {
            int quota = 0;
            if (!parse_int(value, quota, error, "cpu_quota")) return false;
            if (quota > 0) {
                config.cpu_limit.quota_percent = quota;
            }
        }
    }

    out = config;
    return true;
}

std::string encode_event(const WorkerEvent& event) {
    std::string out(event_type_name(event.type));
    for (const auto& [key, value] : event.fields) {
        out.push_back(' ');
        out += key;
        out.push_back('=');
        out += quote_value(value);
    }
    return out;
}

std::optional<WorkerEvent> parse_event(std::string_view line, std::string* error) {
    line = trim(line);
    if (line.empty()) {
        set_error(error, "evento vacio");
        return std::nullopt;
    }
    if (line.size() > kMaxProtocolLine) {
        set_error(error, "evento demasiado largo");
        return std::nullopt;
    }

    const std::size_t space = line.find_first_of(" \t");
    const std::string_view name = line.substr(0, space);
    const std::string_view rest =
        (space == std::string_view::npos) ? std::string_view{} 
                                          : line.substr(space + 1);
    const auto type = event_type_from_name(name);
    if (!type) {
        set_error(error, "evento desconocido");
        return std::nullopt;
    }

    WorkerEvent event{*type, {}};
    if (!parse_fields(rest, event.fields, error)) {
        return std::nullopt;
    }
    return event;
}

std::string limit_status_name(LimitStatus status) {
    switch (status) {
        case LimitStatus::NotRequested: return "not_requested";
        case LimitStatus::Applied: return "applied";
        case LimitStatus::NotApplied: return "not_applied";
        case LimitStatus::Unsupported: return "unsupported";
    }
    return "not_requested";
}

std::optional<LimitStatus> parse_limit_status(std::string_view text) {
    if (text == "not_requested") return LimitStatus::NotRequested;
    if (text == "applied") return LimitStatus::Applied;
    if (text == "not_applied") return LimitStatus::NotApplied;
    if (text == "unsupported") return LimitStatus::Unsupported;
    return std::nullopt;
}

std::string run_outcome_name(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Completed: return "completed";
        case RunOutcome::Stopped: return "stopped";
        case RunOutcome::MemoryLimitHit: return "memory_limit";
        case RunOutcome::CpuLimitHit: return "cpu_limit";
        case RunOutcome::ExternalKill: return "external_kill";
        case RunOutcome::Error: return "error";
    }
    return "error";
}

std::optional<RunOutcome> parse_run_outcome(std::string_view text) {
    if (text == "completed") return RunOutcome::Completed;
    if (text == "stopped") return RunOutcome::Stopped;
    if (text == "memory_limit" || text == "memory_limit_hit") {
        return RunOutcome::MemoryLimitHit;
    }
    if (text == "cpu_limit" || text == "cpu_limit_hit") {
        return RunOutcome::CpuLimitHit;
    }
    if (text == "external_kill") return RunOutcome::ExternalKill;
    if (text == "error") return RunOutcome::Error;
    return std::nullopt;
}

}  // namespace memtest

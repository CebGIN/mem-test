#include <cstdint>
#include <string>

#include "Suites.hpp"
#include "process/WorkerProtocol.hpp"

namespace memtest::test {
namespace {

void config_roundtrip(Context& context) {
    ExperimentConfig config;
    config.workload = WorkloadKind::MemoryPressure;
    config.worker_mode = WorkerMode::RuntimeTasks;
    config.load_mode = LoadMode::TotalWork;
    config.sync_mode = SyncMode::Atomic;
    config.threads = 3;
    config.work_units = 1234;
    config.increments_per_thread = 5678;
    config.memory_bytes = 64ull * 1024ull * 1024ull;
    config.page_stride_bytes = 8192;
    config.force_swap = true;
    config.wall_timeout_ms = 1500;
    config.duration_ms = 3500;
    config.memory_limit_kind = MemoryLimitKind::AddressSpace;
    config.memory_limit_bytes = 96ull * 1024ull * 1024ull;
    config.cpu_limit.seconds = 7;
    config.cpu_limit.quota_percent = 50;

    const std::string line = encode_config(config);
    CHECK(context, line.size() < kMaxProtocolLine);
    CHECK(context, line.find('\n') == std::string::npos);

    ExperimentConfig parsed;
    std::string error;
    CHECK(context, parse_config(line, parsed, &error));
    CHECK(context, error.empty());
    CHECK_EQ(context, parsed.workload, config.workload);
    CHECK_EQ(context, parsed.worker_mode, config.worker_mode);
    CHECK_EQ(context, parsed.load_mode, config.load_mode);
    CHECK_EQ(context, parsed.sync_mode, config.sync_mode);
    CHECK_EQ(context, parsed.threads, config.threads);
    CHECK_EQ(context, parsed.work_units, config.work_units);
    CHECK_EQ(context, parsed.increments_per_thread,
             config.increments_per_thread);
    CHECK_EQ(context, parsed.memory_bytes, config.memory_bytes);
    CHECK_EQ(context, parsed.page_stride_bytes, config.page_stride_bytes);
    CHECK_EQ(context, parsed.force_swap, config.force_swap);
    CHECK_EQ(context, parsed.wall_timeout_ms, config.wall_timeout_ms);
    CHECK_EQ(context, parsed.duration_ms, config.duration_ms);
    CHECK_EQ(context, parsed.memory_limit_kind, config.memory_limit_kind);
    CHECK_EQ(context, parsed.memory_limit_bytes, config.memory_limit_bytes);
    CHECK_EQ(context, parsed.cpu_limit.seconds.value_or(0), 7);
    CHECK_EQ(context, parsed.cpu_limit.quota_percent.value_or(0), 50);
}

void config_rejects_bad_values(Context& context) {
    ExperimentConfig parsed;
    std::string error;
    CHECK(context, !parse_config("", parsed, &error));
    CHECK(context, !error.empty());

    error.clear();
    CHECK(context, !parse_config("threads=no-es-un-numero", parsed, &error));
    CHECK(context, !error.empty());

    error.clear();
    CHECK(context, !parse_config("workload=desconocido", parsed, &error));
    CHECK(context, !error.empty());
}

void events_roundtrip(Context& context) {
    WorkerEvent ready;
    ready.type = WorkerEvent::Type::Ready;
    ready.fields["baseline_vm"] = "12345";
    ready.fields["baseline_rss"] = "6789";
    std::string error;
    auto parsed = parse_event(encode_event(ready), &error);
    CHECK(context, parsed.has_value());
    CHECK(context, error.empty());
    if (parsed) {
        CHECK_EQ(context, parsed->type, WorkerEvent::Type::Ready);
        CHECK_EQ(context, parsed->fields["baseline_vm"], std::string("12345"));
        CHECK_EQ(context, parsed->fields["baseline_rss"], std::string("6789"));
    }

    WorkerEvent result;
    result.type = WorkerEvent::Type::Result;
    result.fields["outcome"] = "memory_limit";
    result.fields["expected"] = "4096";
    result.fields["observed"] = "2048";
    result.fields["detail"] = "mmap fallo: cannot allocate memory";
    parsed = parse_event(encode_event(result), &error);
    CHECK(context, parsed.has_value());
    if (parsed) {
        CHECK_EQ(context, parsed->type, WorkerEvent::Type::Result);
        CHECK_EQ(context, parsed->fields["detail"],
                 std::string("mmap fallo: cannot allocate memory"));
    }

    parsed = parse_event("END", &error);
    CHECK(context, parsed.has_value());
    if (parsed) {
        CHECK_EQ(context, parsed->type, WorkerEvent::Type::End);
    }
}

void events_reject_bad_input(Context& context) {
    std::string error;
    CHECK(context, !parse_event("", &error).has_value());
    CHECK(context, !error.empty());

    error.clear();
    CHECK(context, !parse_event("DESCONOCIDO", &error).has_value());
    CHECK(context, !error.empty());

    error.clear();
    CHECK(context, !parse_event("RESULT detail=\"sin cerrar", &error).has_value());
    CHECK(context, !error.empty());

    error.clear();
    const std::string long_line(kMaxProtocolLine + 1, 'x');
    CHECK(context, !parse_event(long_line, &error).has_value());
    CHECK(context, !error.empty());
}

void status_and_outcome_names(Context& context) {
    CHECK_EQ(context, limit_status_name(LimitStatus::Applied),
             std::string("applied"));
    CHECK_EQ(context, parse_limit_status("not_applied").value_or(
                         LimitStatus::Applied),
             LimitStatus::NotApplied);
    CHECK_EQ(context, run_outcome_name(RunOutcome::MemoryLimitHit),
             std::string("memory_limit"));
    CHECK_EQ(context, parse_run_outcome("cpu_limit").value_or(
                         RunOutcome::Error),
             RunOutcome::CpuLimitHit);
}

}  // namespace

Suite worker_protocol_suite() {
    return {
        {"configuracion ida y vuelta", config_roundtrip},
        {"configuracion rechaza valores invalidos", config_rejects_bad_values},
        {"eventos ida y vuelta", events_roundtrip},
        {"eventos rechazan entradas invalidas", events_reject_bad_input},
        {"nombres de estado y resultado", status_and_outcome_names},
    };
}

}  // namespace memtest::test

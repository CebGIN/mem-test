#include <cstdint>
#include <string>

#include "Suites.hpp"
#include "domain/Validation.hpp"

namespace memtest::test {
namespace {

constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;
constexpr std::uint64_t kMiB = 1024ull * 1024ull;

GuardrailLimits limits_for(int cores, std::uint64_t total_ram = 8 * kGiB) {
    SystemInfo info;
    info.total_ram = total_ram;
    info.available_ram = total_ram;
    info.cores = cores;
    return guardrails_from(info);
}

ExperimentConfig memory_config() {
    ExperimentConfig config;
    config.workload = WorkloadKind::MemoryPressure;
    return config;
}

bool has_issue(const ValidationReport& report, const std::string& field) {
    for (const ValidationIssue& issue : report.issues) {
        if (issue.field == field) {
            return true;
        }
    }
    return false;
}

void test_defaults(Context& ctx) {
    const ExperimentConfig config;
    CHECK_EQ(ctx, config.workload, WorkloadKind::Concurrency);
    CHECK_EQ(ctx, config.worker_mode, WorkerMode::NativeThreads);
    CHECK_EQ(ctx, config.sync_mode, SyncMode::None);
    CHECK_EQ(ctx, config.threads, 4);
    CHECK_EQ(ctx, config.work_units, std::uint64_t{100000});
    CHECK_EQ(ctx, config.increments_per_thread, std::uint64_t{100000});
    CHECK_EQ(ctx, config.memory_bytes, std::uint64_t{0});
    CHECK_EQ(ctx, config.page_stride_bytes, std::uint64_t{4096});
    CHECK_EQ(ctx, config.wall_timeout_ms, 5000);
    CHECK_EQ(ctx, config.memory_limit_kind, MemoryLimitKind::None);
    CHECK_EQ(ctx, config.memory_limit_bytes, std::uint64_t{0});
    CHECK(ctx, !config.cpu_limit.seconds.has_value());
    CHECK(ctx, !config.cpu_limit.quota_percent.has_value());

    const ValidationReport empty;
    CHECK(ctx, empty.ok());

    const ExperimentResult result;
    CHECK_EQ(ctx, result.outcome, RunOutcome::Error);
    CHECK_EQ(ctx, result.limits.memory_status, LimitStatus::NotRequested);
    CHECK_EQ(ctx, result.limits.cpu_seconds_status, LimitStatus::NotRequested);
    CHECK_EQ(ctx, result.limits.cpu_quota_status, LimitStatus::NotRequested);
}

void test_guardrails(Context& ctx) {
    // 50% de 8 GiB son 4 GiB; ya no hay tope absoluto de 1 GiB.
    const GuardrailLimits big = limits_for(4);
    CHECK_EQ(ctx, big.max_memory_bytes, 4 * kGiB);
    CHECK_EQ(ctx, big.max_threads, 32);
    CHECK_EQ(ctx, big.max_wall_timeout_ms, 120000);
    CHECK_EQ(ctx, big.max_cpu_seconds, 120);

    // 50% de 2 GiB son 1 GiB.
    const GuardrailLimits small = limits_for(2, 2 * kGiB);
    CHECK_EQ(ctx, small.max_memory_bytes, kGiB);
    CHECK_EQ(ctx, small.max_threads, 16);

    // El tope de hilos no crece más allá de 64.
    const GuardrailLimits many = limits_for(64);
    CHECK_EQ(ctx, many.max_threads, 64);

    // El tope tambien respeta la RAM disponible, no solo la total.
    SystemInfo low_available;
    low_available.total_ram = 8 * kGiB;
    low_available.available_ram = 2 * kGiB;
    low_available.cores = 4;
    const GuardrailLimits available = guardrails_from(low_available);
    CHECK_EQ(ctx, available.max_memory_bytes, kGiB);

    // Sin datos del sistema se usan valores conservadores: 4 GiB de RAM.
    const GuardrailLimits fallback = guardrails_from(SystemInfo{});
    CHECK_EQ(ctx, fallback.max_memory_bytes, 2 * kGiB);
    CHECK_EQ(ctx, fallback.max_threads, 8);
}

void test_valid_defaults(Context& ctx) {
    const ValidationReport report = validate(ExperimentConfig{}, limits_for(4));
    CHECK(ctx, report.ok());
}

void test_threads(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig zero;
    zero.threads = 0;
    CHECK(ctx, has_issue(validate(zero, limits), "threads"));

    ExperimentConfig too_many;
    too_many.threads = limits.max_threads + 1;
    CHECK(ctx, has_issue(validate(too_many, limits), "threads"));

    ExperimentConfig at_limit;
    at_limit.threads = limits.max_threads;
    CHECK(ctx, validate(at_limit, limits).ok());

    ExperimentConfig counter;
    counter.workload = WorkloadKind::SharedCounter;
    counter.threads = 1;
    CHECK(ctx, has_issue(validate(counter, limits), "threads"));
    counter.threads = 2;
    CHECK(ctx, validate(counter, limits).ok());
}

void test_work_units(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig concurrency;
    concurrency.work_units = 0;
    CHECK(ctx, has_issue(validate(concurrency, limits), "work_units"));

    ExperimentConfig memory = memory_config();
    memory.work_units = 0;
    CHECK(ctx, validate(memory, limits).ok());

    ExperimentConfig total;
    total.load_mode = LoadMode::TotalWork;
    total.work_units = 0;
    CHECK(ctx, has_issue(validate(total, limits), "work_units"));
    total.work_units = 1;
    CHECK(ctx, validate(total, limits).ok());
}

void test_increments(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig counter;
    counter.workload = WorkloadKind::SharedCounter;
    counter.increments_per_thread = 1;
    CHECK(ctx, validate(counter, limits).ok());

    counter.increments_per_thread = 0;
    CHECK(ctx, has_issue(validate(counter, limits), "increments"));
}

void test_wall_timeout(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig zero;
    zero.wall_timeout_ms = 0;
    CHECK(ctx, has_issue(validate(zero, limits), "wall_timeout_ms"));

    ExperimentConfig too_long;
    too_long.wall_timeout_ms = limits.max_wall_timeout_ms + 1;
    CHECK(ctx, has_issue(validate(too_long, limits), "wall_timeout_ms"));

    ExperimentConfig at_limit;
    at_limit.wall_timeout_ms = limits.max_wall_timeout_ms;
    CHECK(ctx, validate(at_limit, limits).ok());
}

void test_memory_request(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig too_big = memory_config();
    too_big.memory_bytes = limits.max_memory_bytes + 1;
    CHECK(ctx, has_issue(validate(too_big, limits), "memory_bytes"));

    ExperimentConfig fits = memory_config();
    fits.memory_bytes = 256 * kMiB;
    CHECK(ctx, validate(fits, limits).ok());

    // El tope ahora es la mitad de la RAM (4 GiB con 8 GiB): se permiten
    // reservas por encima de 1 GiB.
    ExperimentConfig at_limit = memory_config();
    at_limit.memory_bytes = limits.max_memory_bytes;
    CHECK(ctx, validate(at_limit, limits).ok());

    ExperimentConfig two_gib = memory_config();
    two_gib.memory_bytes = 2 * kGiB;
    CHECK(ctx, validate(two_gib, limits).ok());

    // El paso de página no puede ser cero: el toque de páginas no avanzaría.
    ExperimentConfig stride = memory_config();
    stride.page_stride_bytes = 0;
    CHECK(ctx, has_issue(validate(stride, limits), "page_stride"));
}

void test_memory_limit(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig zero_limit = memory_config();
    zero_limit.memory_limit_kind = MemoryLimitKind::AddressSpace;
    CHECK(ctx, has_issue(validate(zero_limit, limits), "memory_limit_bytes"));

    ExperimentConfig too_big = memory_config();
    too_big.memory_limit_kind = MemoryLimitKind::DataSegment;
    too_big.memory_limit_bytes = limits.max_memory_bytes + 1;
    CHECK(ctx, has_issue(validate(too_big, limits), "memory_limit_bytes"));

    // Una reserva mayor que el límite es válida: el worker debe reportar
    // MemoryLimitHit. El laboratorio necesita poder ejecutar ese caso.
    ExperimentConfig smaller_than_reservation = memory_config();
    smaller_than_reservation.memory_limit_kind = MemoryLimitKind::AddressSpace;
    smaller_than_reservation.memory_bytes = 256 * kMiB;
    smaller_than_reservation.memory_limit_bytes = 128 * kMiB;
    CHECK(ctx, validate(smaller_than_reservation, limits).ok());

    ExperimentConfig ok = memory_config();
    ok.memory_limit_kind = MemoryLimitKind::AddressSpace;
    ok.memory_bytes = 128 * kMiB;
    ok.memory_limit_bytes = 512 * kMiB;
    CHECK(ctx, validate(ok, limits).ok());
}

void test_duration(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig legacy = ExperimentConfig{};
    legacy.work_units = 0;
    legacy.duration_ms = 0;
    CHECK(ctx, has_issue(validate(legacy, limits), "work_units"));

    ExperimentConfig timed = ExperimentConfig{};
    timed.work_units = 0;
    timed.increments_per_thread = 0;
    timed.duration_ms = 2000;
    timed.wall_timeout_ms = 5000;
    CHECK(ctx, validate(timed, limits).ok());

    ExperimentConfig too_long = timed;
    too_long.duration_ms = limits.max_wall_timeout_ms;
    CHECK(ctx, has_issue(validate(too_long, limits), "duration_ms"));

    ExperimentConfig short_watchdog = timed;
    short_watchdog.wall_timeout_ms = short_watchdog.duration_ms;
    CHECK(ctx, has_issue(validate(short_watchdog, limits), "wall_timeout_ms"));
}

void test_cpu_limits(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig zero_seconds;
    zero_seconds.cpu_limit.seconds = 0;
    CHECK(ctx, has_issue(validate(zero_seconds, limits), "cpu_seconds"));

    ExperimentConfig too_many_seconds;
    too_many_seconds.cpu_limit.seconds = limits.max_cpu_seconds + 1;
    CHECK(ctx, has_issue(validate(too_many_seconds, limits), "cpu_seconds"));

    ExperimentConfig valid_seconds;
    valid_seconds.cpu_limit.seconds = 3;
    CHECK(ctx, validate(valid_seconds, limits).ok());

    ExperimentConfig zero_quota;
    zero_quota.cpu_limit.quota_percent = 0;
    CHECK(ctx, has_issue(validate(zero_quota, limits), "cpu_quota"));

    ExperimentConfig high_quota;
    high_quota.cpu_limit.quota_percent = 101;
    CHECK(ctx, has_issue(validate(high_quota, limits), "cpu_quota"));

    ExperimentConfig valid_quota;
    valid_quota.cpu_limit.quota_percent = 50;
    CHECK(ctx, validate(valid_quota, limits).ok());

    ExperimentConfig cpu_test;
    cpu_test.workload = WorkloadKind::CpuLimit;
    cpu_test.threads = 1;
    CHECK(ctx, has_issue(validate(cpu_test, limits), "cpu_limit"));
    cpu_test.cpu_limit.seconds = 5;
    CHECK(ctx, validate(cpu_test, limits).ok());
    cpu_test.threads = 2;
    CHECK(ctx, has_issue(validate(cpu_test, limits), "threads"));
}

void test_reports_every_issue(Context& ctx) {
    const GuardrailLimits limits = limits_for(4);

    ExperimentConfig broken;
    broken.threads = 0;
    broken.work_units = 0;
    broken.wall_timeout_ms = 0;

    const ValidationReport report = validate(broken, limits);
    CHECK(ctx, !report.ok());
    CHECK(ctx, report.issues.size() >= 3);
    CHECK(ctx, has_issue(report, "threads"));
    CHECK(ctx, has_issue(report, "work_units"));
    CHECK(ctx, has_issue(report, "wall_timeout_ms"));
}

}  // namespace

Suite domain_suite() {
    return {
        {"defaults", test_defaults},
        {"guardrails", test_guardrails},
        {"valid_defaults", test_valid_defaults},
        {"threads", test_threads},
        {"work_units", test_work_units},
        {"increments", test_increments},
        {"wall_timeout", test_wall_timeout},
        {"memory_request", test_memory_request},
        {"memory_limit", test_memory_limit},
        {"duration", test_duration},
        {"cpu_limits", test_cpu_limits},
        {"reports_every_issue", test_reports_every_issue},
    };
}

}  // namespace memtest::test

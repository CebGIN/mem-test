// Pruebas de limits/ (Linux y Windows). Los límites afectan al proceso que los
// aplica, así que cada escenario corre en un proceso hijo: esta misma
// ejecutable relanzada con `--child <escenario>`. El código de salida del hijo
// es el resultado:
//   0   el escenario salió como se esperaba sin tocar nada
//   10  la reserva de memoria falló por el límite   (memory_limit)
//   11  el aviso blando de CPU llegó                 (cpu_limit)
//   2x  precondición no cumplida (el entorno no deja aplicar el límite)
//   99  el límite nunca actuó
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "TestHarness.hpp"
#include "limits/CpuLimitNotifier.hpp"
#include "limits/ResourceLimiter.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#endif

using namespace memtest;
using memtest::limits::ResourceLimiter;

namespace {

constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr int kExitMemoryLimit = 10;
constexpr int kExitCpuLimit = 11;
constexpr int kExitNotApplied = 20;
constexpr int kExitSmallAllocFailed = 21;
constexpr int kExitUnexpected = 22;
constexpr int kExitSurvived = 99;

// --- Lado hijo -------------------------------------------------------------

// Intenta reservar y tocar `bytes`. true si lo consiguió.
bool try_allocate(std::uint64_t bytes) {
#ifdef _WIN32
    void* p = VirtualAlloc(nullptr, static_cast<SIZE_T>(bytes),
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p == nullptr) return false;
    auto* b = static_cast<volatile unsigned char*>(p);
    for (std::uint64_t off = 0; off < bytes; off += 4096) b[off] = 1;
    return true;
#else
    void* p = mmap(nullptr, static_cast<std::size_t>(bytes),
                   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return false;
    auto* b = static_cast<volatile unsigned char*>(p);
    for (std::uint64_t off = 0; off < bytes; off += 4096) b[off] = 1;
    return true;
#endif
}

int child_memory(MemoryLimitKind kind) {
    ExperimentConfig cfg;
    cfg.memory_limit_kind = kind;
    cfg.memory_limit_bytes = 256 * kMiB;
    const LimitReport report = ResourceLimiter().apply(cfg);
    if (report.memory_status != LimitStatus::Applied ||
        report.memory_applied != cfg.memory_limit_bytes) {
        return kExitNotApplied;
    }
    if (!try_allocate(16 * kMiB)) return kExitSmallAllocFailed;  // debe caber
    return try_allocate(512 * kMiB) ? kExitSurvived : kExitMemoryLimit;
}

int busy_until(bool (*stop)(), std::chrono::seconds guard) {
    volatile std::uint64_t sink = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!stop()) {
        for (int i = 0; i < 200000; ++i) sink = sink + i;
        if (std::chrono::steady_clock::now() - start > guard) return kExitSurvived;
    }
    return 0;
}

int child_cpu_soft() {
    ExperimentConfig cfg;
    cfg.cpu_limit.seconds = 1;
    const LimitReport report = ResourceLimiter().apply(cfg);
    if (report.cpu_seconds_status != LimitStatus::Applied) return kExitNotApplied;
    if (!limits::install_cpu_limit_notifier(1)) return kExitUnexpected;
    const int rc = busy_until([] { return limits::cpu_limit_reached(); },
                              std::chrono::seconds(30));
    return rc == 0 ? kExitCpuLimit : rc;
}

int child_cpu_hard() {
    ExperimentConfig cfg;
    cfg.cpu_limit.seconds = 1;
    const LimitReport report = ResourceLimiter().apply(cfg);
    if (report.cpu_seconds_status != LimitStatus::Applied) return kExitNotApplied;
#ifndef _WIN32
    std::signal(SIGXCPU, SIG_IGN);  // ignora el aviso: debe llegar el duro
#endif
    return busy_until([] { return false; }, std::chrono::seconds(30));
}

#ifndef _WIN32
// Baja el máximo del proceso y pide un límite por encima: setrlimit lo rechaza
// (EPERM) y el limitador debe informar NotApplied sin abortar.
int child_not_applied() {
    struct rlimit low;
    low.rlim_cur = 200 * kMiB;
    low.rlim_max = 200 * kMiB;
    if (setrlimit(RLIMIT_AS, &low) != 0) return kExitUnexpected;

    ExperimentConfig cfg;
    cfg.memory_limit_kind = MemoryLimitKind::AddressSpace;
    cfg.memory_limit_bytes = 400 * kMiB;
    std::string detail;
    const LimitReport report = ResourceLimiter().apply(cfg, &detail);
    if (report.memory_status != LimitStatus::NotApplied) return kExitUnexpected;
    if (report.memory_applied != 0) return kExitUnexpected;
    if (detail.empty()) return kExitUnexpected;
    return 0;
}
#endif

int run_child(const std::string& scenario) {
    if (scenario == "mem-as") return child_memory(MemoryLimitKind::AddressSpace);
    if (scenario == "mem-data") return child_memory(MemoryLimitKind::DataSegment);
    if (scenario == "cpu-soft") return child_cpu_soft();
    if (scenario == "cpu-hard") return child_cpu_hard();
#ifndef _WIN32
    if (scenario == "not-applied") return child_not_applied();
#endif
    return kExitUnexpected;
}

// --- Lado padre ------------------------------------------------------------

struct ChildResult {
    bool exited = false;
    int exit_code = -1;
    int signal = 0;
};

ChildResult launch(const std::string& exe, const std::string& scenario) {
    ChildResult r;
#ifdef _WIN32
    // cmd.exe quita las comillas externas: se envuelve todo en un par extra.
    const std::string cmd = "\"\"" + exe + "\" --child " + scenario + "\"";
    const int rc = std::system(cmd.c_str());
    r.exited = true;
    r.exit_code = rc;
#else
    // `exec` evita que sh quede entre medias y oculte la señal que mató al hijo.
    const std::string cmd = "exec \"" + exe + "\" --child " + scenario;
    const int status = std::system(cmd.c_str());
    if (WIFEXITED(status)) {
        r.exited = true;
        r.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r.signal = WTERMSIG(status);
    }
#endif
    return r;
}

// Evalúa una precondición: si el entorno no deja aplicar el límite, la prueba
// se salta (Arquitectura.md §16: sin root ni cgroups, sin fallos falsos).
bool skipped(const ChildResult& r, const char* name) {
    if (r.exited && r.exit_code == kExitNotApplied) {
        std::cout << "SKIP " << name << ": el entorno no permite aplicar el limite\n";
        return true;
    }
    return false;
}

void test_memory_limit(const std::string& exe, const char* scenario,
                       bool enforcement_may_be_absent) {
    const ChildResult r = launch(exe, scenario);
    if (skipped(r, scenario)) return;
    if (enforcement_may_be_absent && r.exited && r.exit_code == kExitSurvived) {
        std::cout << "SKIP " << scenario
                  << ": este kernel no aplica el limite a mmap\n";
        return;
    }
    CHECK(r.exited);
    CHECK_EQ(r.exit_code, kExitMemoryLimit);
}

void test_cpu_soft(const std::string& exe) {
    const ChildResult r = launch(exe, "cpu-soft");
    if (skipped(r, "cpu-soft")) return;
    CHECK(r.exited);
    CHECK_EQ(r.exit_code, kExitCpuLimit);
}

void test_cpu_hard(const std::string& exe) {
    const ChildResult r = launch(exe, "cpu-hard");
    if (skipped(r, "cpu-hard")) return;
    // La carga ignora el aviso: el límite duro debe haberla matado.
    CHECK(!(r.exited && (r.exit_code == kExitSurvived || r.exit_code == 0)));
#ifndef _WIN32
    CHECK(!r.exited);
    CHECK_EQ(r.signal, SIGKILL);
#endif
}

#ifndef _WIN32
void test_not_applied(const std::string& exe) {
    const ChildResult r = launch(exe, "not-applied");
    CHECK(r.exited);
    CHECK_EQ(r.exit_code, 0);
}
#endif

// Pruebas que no necesitan proceso hijo: no tocan los límites del proceso.
void test_nothing_requested() {
    std::string detail = "x";
    const LimitReport r = ResourceLimiter().apply(ExperimentConfig{}, &detail);
    CHECK(r.memory_kind == MemoryLimitKind::None);
    CHECK(r.memory_status == LimitStatus::NotRequested);
    CHECK(r.cpu_seconds_status == LimitStatus::NotRequested);
    CHECK(r.cpu_quota_status == LimitStatus::NotRequested);
    CHECK(detail.empty());
}

void test_invalid_and_unsupported_requests() {
    ExperimentConfig cfg;
    cfg.memory_limit_kind = MemoryLimitKind::AddressSpace;
    cfg.memory_limit_bytes = 0;      // tipo pedido, pero sin valor
    cfg.cpu_limit.seconds = 0;       // < 1 s
    cfg.cpu_limit.quota_percent = 50;
    std::string detail;
    const LimitReport r = ResourceLimiter().apply(cfg, &detail);
    CHECK(r.memory_status == LimitStatus::NotApplied);
    CHECK(r.cpu_seconds_status == LimitStatus::NotApplied);
    // La cuota cgroup la aplica el worker con CgroupCpuQuota.
    CHECK(r.cpu_quota_status == LimitStatus::NotRequested);
    CHECK(!detail.empty());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--child") {
        return run_child(argv[2]);
    }

    const std::string exe = std::filesystem::absolute(argv[0]).string();

    test_nothing_requested();
    test_invalid_and_unsupported_requests();
    test_memory_limit(exe, "mem-as", false);
    // RLIMIT_DATA solo cuenta mmap anónimo desde Linux 4.7.
    test_memory_limit(exe, "mem-data", true);
    test_cpu_soft(exe);
    test_cpu_hard(exe);
#ifndef _WIN32
    test_not_applied(exe);
#endif
    return TEST_RESULT();
}

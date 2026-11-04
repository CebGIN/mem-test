// Prueba multiplataforma (Linux y Windows) de IMetricsSource: el proceso de la
// prueba se mide a sí mismo antes y después de reservar y tocar 64 MiB.
#include <chrono>
#include <cstdint>
#include <vector>

#include "TestHarness.hpp"
#include "metrics/MetricsSourceFactory.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace memtest;

namespace {

constexpr std::uint64_t kMiB = 1024 * 1024;

pid_t self_pid() {
#ifdef _WIN32
    return static_cast<pid_t>(GetCurrentProcessId());
#else
    return getpid();
#endif
}

void test_system_info() {
    const SystemInfo info = make_metrics_source()->system_info();
    CHECK(info.total_ram > 0);
    CHECK(info.available_ram > 0);
    CHECK(info.available_ram <= info.total_ram);
    CHECK(info.cores >= 1);
}

void test_self_sample_grows_with_memory() {
    auto source = make_metrics_source();
    MetricsSnapshot before;
    CHECK(source->sample(self_pid(), before));
    CHECK(before.vm_rss > 0);
    CHECK(before.vm_size >= before.vm_rss);
    CHECK(before.threads >= 1);

    // vector(n, 1) escribe cada byte: las páginas quedan residentes.
    std::vector<unsigned char> block(64 * kMiB, 1);

    MetricsSnapshot after;
    CHECK(source->sample(self_pid(), after));
    CHECK(after.vm_rss >= before.vm_rss + 60 * kMiB);
    CHECK(after.vm_size >= before.vm_size + 60 * kMiB);
    CHECK(after.minor_faults > before.minor_faults + 10);
    CHECK(after.vm_hwm >= after.vm_rss);
    CHECK(block[block.size() / 2] == 1);  // evita que se optimice el bloque
}

void test_cpu_time_increases() {
    auto source = make_metrics_source();
    MetricsSnapshot a;
    CHECK(source->sample(self_pid(), a));

    volatile std::uint64_t sink = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(300)) {
        for (int i = 0; i < 100000; ++i) sink = sink + i;
    }

    MetricsSnapshot b;
    CHECK(source->sample(self_pid(), b));
    CHECK((b.cpu_user_s + b.cpu_sys_s) > (a.cpu_user_s + a.cpu_sys_s));
}

void test_sample_keeps_elapsed_and_rejects_missing_pid() {
    auto source = make_metrics_source();
    MetricsSnapshot snap;
    snap.elapsed = std::chrono::milliseconds(1234);
    CHECK(source->sample(self_pid(), snap));
    CHECK(snap.elapsed.count() == 1234);  // lo pone quien llama

    MetricsSnapshot none;
    CHECK(!source->sample(static_cast<pid_t>(0x7ffffffe), none));
}

}  // namespace

int main() {
    test_system_info();
    test_self_sample_grows_with_memory();
    test_cpu_time_increases();
    test_sample_keeps_elapsed_and_rejects_missing_pid();
    return TEST_RESULT();
}

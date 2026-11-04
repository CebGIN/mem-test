// Pruebas de metrics/ para Linux: parsers de /proc con fixtures.
#include <cstdint>
#include <string>

#include "TestHarness.hpp"
#include "metrics/ProcFs.hpp"

using namespace memtest::metrics;

namespace {

constexpr std::uint64_t kKiB = 1024;
constexpr std::uint64_t kMiB = 1024 * 1024;

std::string fixture(const std::string& name) {
    std::string data;
    const bool ok = read_file(std::string(MEMTEST_FIXTURES_DIR) + "/" + name, data);
    CHECK(ok);
    return data;
}

void test_parse_stat_fixtures() {
    ProcStat st;
    CHECK(parse_stat(fixture("proc-stat-basic.txt"), st));
    CHECK(st.comm == "mem-test-worker");
    CHECK_EQ(st.minflt, 5000u);
    CHECK_EQ(st.majflt, 3u);
    CHECK_EQ(st.utime_ticks, 250u);
    CHECK_EQ(st.stime_ticks, 40u);
    CHECK_EQ(st.num_threads, 6);
    CHECK_EQ(st.vsize_bytes, 281400000u);
    CHECK_EQ(st.rss_pages, 17520);

    // `comm` con espacios y paréntesis no debe desplazar los campos.
    ProcStat spaced;
    CHECK(parse_stat(fixture("proc-stat-comm-spaces.txt"), spaced));
    CHECK(spaced.comm == "my worker (x)");
    CHECK_EQ(spaced.ppid, 1);
    CHECK_EQ(spaced.minflt, 5000u);
    CHECK_EQ(spaced.utime_ticks, 250u);
    CHECK_EQ(spaced.rss_pages, 17520);

    CHECK(!parse_stat("basura", st));
}

void test_parse_meminfo_and_loadavg() {
    MemInfo m;
    CHECK(parse_meminfo(fixture("proc-meminfo-with-swap.txt"), m));
    CHECK_EQ(m.total_bytes, 16384000 * kKiB);
    CHECK_EQ(m.available_bytes, 9000000 * kKiB);
    CHECK_EQ(m.swap_total_bytes, 4194304 * kKiB);
    CHECK_EQ(m.swap_free_bytes, 4000000 * kKiB);

    CHECK(parse_meminfo(fixture("proc-meminfo-no-swap.txt"), m));
    CHECK_EQ(m.swap_total_bytes, 0u);  // SWAP no disponible

    // Sin MemAvailable: MemFree + Buffers + Cached.
    CHECK(parse_meminfo(fixture("proc-meminfo-no-available.txt"), m));
    CHECK_EQ(m.available_bytes, (500000 + 100000 + 900000) * kKiB);

    CHECK(!parse_meminfo("nada\n", m));

    double load = 0.0;
    CHECK(parse_load_average(fixture("proc-loadavg.txt"), load));
    CHECK(load > 0.51 && load < 0.53);
    CHECK(!parse_load_average("", load));
}

}  // namespace

int main() {
    test_parse_stat_fixtures();
    test_parse_meminfo_and_loadavg();
    return TEST_RESULT();
}

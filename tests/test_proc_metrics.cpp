// Pruebas de metrics/ para Linux: parsers con fixtures de /proc y un hijo real
// que reserva y toca 64 MiB (criterio de aceptación del WP1).
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <string>

#include "TestHarness.hpp"
#include "metrics/MetricsSourceFactory.hpp"
#include "metrics/ProcFs.hpp"
#include "metrics/ProcMetricsSource.hpp"

using namespace memtest;
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

void test_parse_status_full() {
    ProcStatus s;
    CHECK(parse_status(fixture("proc-status-full.txt"), s));
    CHECK_EQ(s.vm_size, 281400 * kKiB);
    CHECK_EQ(s.vm_rss, 70080 * kKiB);
    CHECK_EQ(s.vm_hwm, 70200 * kKiB);
    CHECK_EQ(s.vm_swap, 1024 * kKiB);
    CHECK_EQ(s.rss_anon, 65800 * kKiB);
    CHECK_EQ(s.rss_file, 4280 * kKiB);
    CHECK_EQ(s.rss_shmem, 0u);
    CHECK_EQ(s.threads, 5);
    CHECK_EQ(s.voluntary_ctxt, 12u);
    CHECK_EQ(s.involuntary_ctxt, 345u);
    CHECK(s.has_vm_size && s.has_vm_rss && s.has_threads);
}

void test_parse_status_old_kernel() {
    ProcStatus s;
    CHECK(parse_status(fixture("proc-status-old-kernel.txt"), s));
    CHECK_EQ(s.vm_rss, 2900 * kKiB);
    CHECK_EQ(s.rss_anon, 0u);  // campo ausente -> 0, no error
    CHECK_EQ(s.vm_swap, 0u);
}

void test_parse_status_zombie() {
    ProcStatus s;
    CHECK(parse_status(fixture("proc-status-zombie.txt"), s));
    CHECK(!s.has_vm_rss);
    CHECK(!s.has_vm_size);
    CHECK(s.has_threads);
    CHECK_EQ(s.threads, 1);
}

void test_parse_status_rejects_bad_input() {
    ProcStatus s;
    CHECK(!parse_status(fixture("proc-status-bad-number.txt"), s));
    CHECK(!parse_status(fixture("proc-status-overflow.txt"), s));
    CHECK(!parse_status("", s));
    CHECK(!parse_status("esto no es un status\n", s));
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

// --- Integración: hijo real ------------------------------------------------

// El hijo avisa con una letra por `ready`; espera la orden por `go`.
//   'A' baseline listo -> el padre muestrea -> 'g'
//   el hijo reserva y toca 64 MiB -> 'B' -> el padre muestrea -> 'q' y sale
[[noreturn]] void child_main(int ready_fd, int go_fd) {
    char cmd = 0;
    (void)!write(ready_fd, "A", 1);
    if (read(go_fd, &cmd, 1) != 1) _exit(2);

    const std::size_t bytes = 64 * kMiB;
    void* region = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) _exit(3);
    madvise(region, bytes, MADV_NOHUGEPAGE);  // un fallo por página de 4 KiB
    auto* bytes_ptr = static_cast<volatile unsigned char*>(region);
    for (std::size_t off = 0; off < bytes; off += 4096) {
        bytes_ptr[off] = 1;
    }
    (void)!write(ready_fd, "B", 1);
    if (read(go_fd, &cmd, 1) != 1) _exit(4);
    _exit(0);
}

void test_child_allocating_64_mib() {
    int ready[2];
    int go[2];
    CHECK(pipe(ready) == 0);
    CHECK(pipe(go) == 0);

    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(ready[0]);
        close(go[1]);
        child_main(ready[1], go[0]);
    }
    close(ready[1]);
    close(go[0]);

    auto source = make_metrics_source();
    char tag = 0;
    MetricsSnapshot before;
    MetricsSnapshot after;

    CHECK(read(ready[0], &tag, 1) == 1 && tag == 'A');
    CHECK(source->sample(child, before));
    CHECK(write(go[1], "g", 1) == 1);
    CHECK(read(ready[0], &tag, 1) == 1 && tag == 'B');
    CHECK(source->sample(child, after));

    CHECK(after.vm_rss >= before.vm_rss + 60 * kMiB);
    CHECK(after.vm_size >= before.vm_size + 60 * kMiB);
    CHECK(after.minor_faults > before.minor_faults + 1000);
    CHECK(after.vm_hwm >= after.vm_rss);
    CHECK(after.rss_anon >= before.rss_anon + 60 * kMiB);
    CHECK(after.threads >= 1);

    CHECK(write(go[1], "q", 1) == 1);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(ready[0]);
    close(go[1]);

    // Con el hijo ya recogido, el proceso no existe: sample debe fallar.
    MetricsSnapshot gone;
    CHECK(!source->sample(child, gone));
}

void test_system_info_on_linux() {
    const SystemInfo info = make_metrics_source()->system_info();
    CHECK(info.total_ram > 0);
    CHECK(info.available_ram > 0 && info.available_ram <= info.total_ram);
    CHECK(info.cores >= 1);
}

}  // namespace

int main() {
    test_parse_status_full();
    test_parse_status_old_kernel();
    test_parse_status_zombie();
    test_parse_status_rejects_bad_input();
    test_parse_stat_fixtures();
    test_parse_meminfo_and_loadavg();
    test_child_allocating_64_mib();
    test_system_info_on_linux();
    return TEST_RESULT();
}

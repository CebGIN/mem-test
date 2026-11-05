#include "metrics/ProcMetricsSource.hpp"

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include "metrics/ProcFs.hpp"

namespace memtest::metrics {

namespace {

constexpr std::uint64_t kBytesPerKib = 1024;

// Entero sin signo del inicio de `text`. Devuelve false si no hay dígitos o si
// el valor no cabe en uint64 (errc::result_out_of_range). `rest` queda justo
// después de los dígitos.
bool parse_u64(std::string_view text, std::uint64_t& value,
               std::string_view& rest) {
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{}) {
        return false;
    }
    rest = text.substr(static_cast<std::size_t>(result.ptr - first));
    return true;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// Valor de una línea de status: entero con unidad opcional. Si la unidad es
// `kB` se multiplica por 1024 comprobando el desbordamiento.
bool parse_value(std::string_view raw, bool is_size, std::uint64_t& out) {
    std::string_view rest;
    std::uint64_t value = 0;
    if (!parse_u64(trim(raw), value, rest)) {
        return false;
    }
    rest = trim(rest);
    if (is_size) {
        if (rest != "kB") {
            return false;
        }
        if (value > std::numeric_limits<std::uint64_t>::max() / kBytesPerKib) {
            return false;
        }
        value *= kBytesPerKib;
    } else if (!rest.empty()) {
        return false;
    }
    out = value;
    return true;
}

double ticks_to_seconds(std::uint64_t ticks) {
    return static_cast<double>(ticks) /
           static_cast<double>(clock_ticks_per_second());
}

}  // namespace

bool parse_status(const std::string& data, ProcStatus& out) {
    ProcStatus parsed;
    std::string_view remaining(data);

    while (!remaining.empty()) {
        const std::size_t eol = remaining.find('\n');
        const std::string_view line = remaining.substr(0, eol);
        remaining = (eol == std::string_view::npos)
                        ? std::string_view{}
                        : remaining.substr(eol + 1);

        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, colon);
        const std::string_view raw = line.substr(colon + 1);

        std::uint64_t value = 0;
        // Se compara la clave completa: `VmSize` no es `VmSwap` ni `VmPeak`.
        if (key == "VmSize") {
            if (!parse_value(raw, true, value)) return false;
            parsed.vm_size = value;
            parsed.has_vm_size = true;
        } else if (key == "VmRSS") {
            if (!parse_value(raw, true, value)) return false;
            parsed.vm_rss = value;
            parsed.has_vm_rss = true;
        } else if (key == "VmHWM") {
            if (!parse_value(raw, true, value)) return false;
            parsed.vm_hwm = value;
        } else if (key == "VmSwap") {
            if (!parse_value(raw, true, value)) return false;
            parsed.vm_swap = value;
        } else if (key == "RssAnon") {
            if (!parse_value(raw, true, value)) return false;
            parsed.rss_anon = value;
        } else if (key == "RssFile") {
            if (!parse_value(raw, true, value)) return false;
            parsed.rss_file = value;
        } else if (key == "RssShmem") {
            if (!parse_value(raw, true, value)) return false;
            parsed.rss_shmem = value;
        } else if (key == "Threads") {
            if (!parse_value(raw, false, value)) return false;
            if (value > static_cast<std::uint64_t>(
                            std::numeric_limits<long>::max())) {
                return false;
            }
            parsed.threads = static_cast<long>(value);
            parsed.has_threads = true;
        } else if (key == "voluntary_ctxt_switches") {
            if (!parse_value(raw, false, value)) return false;
            parsed.voluntary_ctxt = value;
        } else if (key == "nonvoluntary_ctxt_switches") {
            if (!parse_value(raw, false, value)) return false;
            parsed.involuntary_ctxt = value;
        }
    }

    if (!parsed.has_threads && !parsed.has_vm_rss) {
        return false;
    }
    out = parsed;
    return true;
}

bool read_proc_status(pid_t pid, ProcStatus& out) {
    std::string data;
    if (!read_file("/proc/" + std::to_string(pid) + "/status", data)) {
        return false;
    }
    return parse_status(data, out);
}

bool ProcMetricsSource::sample(pid_t pid, MetricsSnapshot& out) const {
    ProcStatus status;
    ProcStat stat;
    // Si el proceso termina entre las dos lecturas, una falla: se descarta la
    // muestra completa para no mezclar instantes distintos.
    if (!read_proc_status(pid, status) || !read_proc_stat(pid, stat)) {
        return false;
    }

    const std::uint64_t page = page_size_bytes();
    MetricsSnapshot snap;
    snap.elapsed = out.elapsed;  // lo gestiona quien llama

    // Zombis e hilos del kernel no tienen líneas Vm*: se recurre a stat.
    snap.vm_size = status.has_vm_size ? status.vm_size : stat.vsize_bytes;
    snap.vm_rss = status.has_vm_rss
                      ? status.vm_rss
                      : static_cast<std::uint64_t>(stat.rss_pages > 0
                                                       ? stat.rss_pages
                                                       : 0) * page;
    snap.vm_hwm = status.vm_hwm;
    snap.vm_swap = status.vm_swap;
    snap.rss_anon = status.rss_anon;
    snap.rss_file = status.rss_file;
    snap.rss_shmem = status.rss_shmem;
    snap.minor_faults = stat.minflt;
    snap.major_faults = stat.majflt;
    snap.cpu_user_s = ticks_to_seconds(stat.utime_ticks);
    snap.cpu_sys_s = ticks_to_seconds(stat.stime_ticks);
    snap.threads = static_cast<int>(status.has_threads ? status.threads
                                                       : stat.num_threads);
    snap.voluntary_ctxt = status.voluntary_ctxt;
    snap.involuntary_ctxt = status.involuntary_ctxt;

    MemInfo mem;
    if (read_meminfo(mem)) {
        snap.sys_swap_total = mem.swap_total_bytes;
        snap.sys_swap_free = mem.swap_free_bytes;
    }
    double load = 0.0;
    if (read_load_average(load)) {
        snap.load_1m = load;
    }

    out = snap;
    return true;
}

bool ProcMetricsSource::sample_system(SystemSnapshot& out) const {
    GlobalStat global;
    MemInfo mem;
    VmStat vm;
    if (!read_global_stat(global) || !read_meminfo(mem)) {
        return false;
    }
    (void)read_vmstat(vm);

    double load_1m = 0.0;
    double load_5m = 0.0;
    double load_15m = 0.0;
    (void)read_load_averages(load_1m, load_5m, load_15m);

    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(system_mutex_);

    SystemSnapshot snap;
    snap.mem_total = mem.total_bytes;
    snap.mem_available = mem.available_bytes;
    snap.mem_used = mem.total_bytes > mem.available_bytes
                        ? mem.total_bytes - mem.available_bytes
                        : 0;
    snap.mem_free = mem.free_bytes;
    snap.swap_total = mem.swap_total_bytes;
    snap.swap_free = mem.swap_free_bytes;
    snap.swap_used = mem.swap_total_bytes > mem.swap_free_bytes
                         ? mem.swap_total_bytes - mem.swap_free_bytes
                         : 0;
    snap.swap_available = mem.swap_total_bytes > 0;
    snap.load_1m = load_1m;
    snap.load_5m = load_5m;
    snap.load_15m = load_15m;
    snap.procs_running = global.procs_running;
    snap.procs_blocked = global.procs_blocked;
    snap.processes = global.processes;
    snap.ctxt = global.ctxt;
    snap.minor_faults = vm.pgfault;
    snap.major_faults = vm.pgmajfault;
    snap.swap_in = vm.pswpin;
    snap.swap_out = vm.pswpout;

    if (!system_primed_) {
        first_sample_ = now;
        system_primed_ = true;
    } else {
        const double seconds =
            std::chrono::duration<double>(now - previous_sample_).count();
        const double safe_seconds = std::max(seconds, 1e-6);

        const std::uint64_t previous_total =
            previous_global_.cpu_user + previous_global_.cpu_nice +
            previous_global_.cpu_system + previous_global_.cpu_idle +
            previous_global_.cpu_iowait + previous_global_.cpu_irq +
            previous_global_.cpu_softirq + previous_global_.cpu_steal;
        const std::uint64_t current_total =
            global.cpu_user + global.cpu_nice + global.cpu_system +
            global.cpu_idle + global.cpu_iowait + global.cpu_irq +
            global.cpu_softirq + global.cpu_steal;
        const std::uint64_t total_delta =
            current_total > previous_total ? current_total - previous_total : 0;

        auto delta = [](std::uint64_t before, std::uint64_t after) {
            return after > before ? after - before : 0;
        };
        auto percent = [total_delta](std::uint64_t part) {
            return total_delta == 0
                       ? 0.0
                       : 100.0 * static_cast<double>(part) /
                             static_cast<double>(total_delta);
        };

        const std::uint64_t user_delta =
            delta(previous_global_.cpu_user, global.cpu_user) +
            delta(previous_global_.cpu_nice, global.cpu_nice);
        const std::uint64_t system_delta =
            delta(previous_global_.cpu_system, global.cpu_system) +
            delta(previous_global_.cpu_irq, global.cpu_irq) +
            delta(previous_global_.cpu_softirq, global.cpu_softirq);
        const std::uint64_t iowait_delta =
            delta(previous_global_.cpu_iowait, global.cpu_iowait);

        if (total_delta == 0) {
            snap.cpu_user_percent = 0.0;
            snap.cpu_system_percent = 0.0;
            snap.cpu_iowait_percent = 0.0;
            snap.cpu_total_percent = 0.0;
        } else {
            snap.cpu_user_percent = percent(user_delta);
            snap.cpu_system_percent = percent(system_delta);
            snap.cpu_iowait_percent = percent(iowait_delta);
            snap.cpu_total_percent = std::min(
                100.0, std::max(0.0,
                                100.0 - percent(delta(previous_global_.cpu_idle,
                                                      global.cpu_idle) +
                                                iowait_delta)));
        }

        snap.ctxt_per_second = static_cast<std::uint64_t>(
            static_cast<double>(delta(previous_global_.ctxt, global.ctxt)) /
            safe_seconds);
        snap.minor_faults_per_second = static_cast<std::uint64_t>(
            static_cast<double>(delta(previous_vm_.pgfault, vm.pgfault)) /
            safe_seconds);
        snap.major_faults_per_second = static_cast<std::uint64_t>(
            static_cast<double>(delta(previous_vm_.pgmajfault, vm.pgmajfault)) /
            safe_seconds);
    }

    snap.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - first_sample_);

    previous_global_ = global;
    previous_vm_ = vm;
    previous_sample_ = now;

    out = snap;
    return true;
}

SystemInfo ProcMetricsSource::system_info() const {
    SystemInfo info;
    MemInfo mem;
    if (read_meminfo(mem)) {
        info.total_ram = mem.total_bytes;
        info.available_ram = mem.available_bytes;
        info.swap_total = mem.swap_total_bytes;
    }
    const long cores = sysconf(_SC_NPROCESSORS_ONLN);
    info.cores = cores > 0 ? static_cast<int>(cores) : 1;
    return info;
}

}  // namespace memtest::metrics

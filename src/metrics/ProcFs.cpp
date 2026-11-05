// Helpers de lectura de /proc adaptados de tasks-lens (mismo equipo).
#include "metrics/ProcFs.hpp"

#include <unistd.h>

#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace memtest::metrics {

namespace {

constexpr std::uint64_t kBytesPerKib = 1024;

// El kernel no cambia estos valores mientras el proceso vive; se consultan una
// sola vez.
std::uint64_t clock_ticks_per_second_impl() {
    static const long hz = sysconf(_SC_CLK_TCK);
    return hz > 0 ? static_cast<std::uint64_t>(hz) : 100u;
}

std::uint64_t page_size_bytes_impl() {
    static const long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? static_cast<std::uint64_t>(ps) : 4096ull;
}

template <typename T>
bool parse_integer(const std::string& token, T& value) {
    const char* first = token.data();
    const char* last = first + token.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

}  // namespace

std::uint64_t clock_ticks_per_second() {
    return clock_ticks_per_second_impl();
}

std::uint64_t ticks_to_ns(std::uint64_t ticks) {
    return ticks * 1000000000ull / clock_ticks_per_second_impl();
}

std::uint64_t page_size_bytes() {
    return page_size_bytes_impl();
}

bool read_file(const std::string& path, std::string& out) {
    out.clear();
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

bool parse_stat(const std::string& data, ProcStat& out) {
    const std::size_t lparen = data.find('(');
    const std::size_t rparen = data.rfind(')');
    if (lparen == std::string::npos || rparen == std::string::npos ||
        rparen < lparen) {
        return false;
    }

    // Los campos posteriores a `comm` se leen desde el último ')'. El primer
    // token es el campo 3 (state); a partir de ahí, índice = campo - 3.
    std::istringstream rest(data.substr(rparen + 1));
    std::vector<std::string> tokens;
    std::string token;
    while (rest >> token) {
        tokens.push_back(token);
    }
    if (tokens.size() < 22) {
        return false;
    }

    ProcStat parsed;
    parsed.comm = data.substr(lparen + 1, rparen - lparen - 1);
    parsed.state = tokens[0].empty() ? '?' : tokens[0][0];
    if (!parse_integer(tokens[1], parsed.ppid) ||
        !parse_integer(tokens[7], parsed.minflt) ||
        !parse_integer(tokens[9], parsed.majflt) ||
        !parse_integer(tokens[11], parsed.utime_ticks) ||
        !parse_integer(tokens[12], parsed.stime_ticks) ||
        !parse_integer(tokens[16], parsed.nice) ||
        !parse_integer(tokens[17], parsed.num_threads) ||
        !parse_integer(tokens[20], parsed.vsize_bytes) ||
        !parse_integer(tokens[21], parsed.rss_pages)) {
        return false;
    }

    out = std::move(parsed);
    return true;
}

bool read_proc_stat(pid_t pid, ProcStat& out) {
    std::string data;
    if (!read_file("/proc/" + std::to_string(pid) + "/stat", data)) {
        return false;
    }
    return parse_stat(data, out);
}

bool parse_meminfo(const std::string& data, MemInfo& out) {
    MemInfo info;
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string key;
        std::uint64_t value = 0;
        std::string unit;
        fields >> key >> value >> unit;

        if (key == "MemTotal:") {
            info.total_bytes = value * kBytesPerKib;
        } else if (key == "MemFree:") {
            info.free_bytes = value * kBytesPerKib;
        } else if (key == "MemAvailable:") {
            info.available_bytes = value * kBytesPerKib;
        } else if (key == "Buffers:") {
            info.buffers_bytes = value * kBytesPerKib;
        } else if (key == "Cached:") {
            info.cached_bytes = value * kBytesPerKib;
        } else if (key == "SwapTotal:") {
            info.swap_total_bytes = value * kBytesPerKib;
        } else if (key == "SwapFree:") {
            info.swap_free_bytes = value * kBytesPerKib;
        }
    }

    if (info.total_bytes == 0) {
        return false;  // no es un /proc/meminfo reconocible
    }
    if (info.available_bytes == 0) {
        info.available_bytes =
            info.free_bytes + info.buffers_bytes + info.cached_bytes;
    }
    out = info;
    return true;
}

bool read_meminfo(MemInfo& out) {
    std::string data;
    return read_file("/proc/meminfo", data) && parse_meminfo(data, out);
}

bool parse_load_average(const std::string& data, double& load_1m) {
    double second = 0.0;
    double third = 0.0;
    return parse_load_averages(data, load_1m, second, third);
}

bool parse_load_averages(const std::string& data, double& load_1m,
                         double& load_5m, double& load_15m) {
    std::istringstream fields(data);
    double first = 0.0;
    double second = 0.0;
    double third = 0.0;
    if (!(fields >> first >> second >> third)) {
        return false;
    }
    load_1m = first;
    load_5m = second;
    load_15m = third;
    return true;
}

bool read_load_average(double& load_1m) {
    std::string data;
    return read_file("/proc/loadavg", data) && parse_load_average(data, load_1m);
}

bool read_load_averages(double& load_1m, double& load_5m, double& load_15m) {
    std::string data;
    return read_file("/proc/loadavg", data) &&
           parse_load_averages(data, load_1m, load_5m, load_15m);
}

bool parse_global_stat(const std::string& data, GlobalStat& out) {
    GlobalStat parsed;
    bool has_cpu = false;
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string key;
        if (!(fields >> key)) {
            continue;
        }
        if (key == "cpu") {
            std::uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
            std::uint64_t irq = 0, softirq = 0, steal = 0;
            if (!(fields >> user >> nice >> system >> idle >> iowait >> irq >>
                  softirq >> steal)) {
                return false;
            }
            parsed.cpu_user = user;
            parsed.cpu_nice = nice;
            parsed.cpu_system = system;
            parsed.cpu_idle = idle;
            parsed.cpu_iowait = iowait;
            parsed.cpu_irq = irq;
            parsed.cpu_softirq = softirq;
            parsed.cpu_steal = steal;
            has_cpu = true;
        } else if (key == "ctxt") {
            if (!(fields >> parsed.ctxt)) return false;
        } else if (key == "processes") {
            if (!(fields >> parsed.processes)) return false;
        } else if (key == "procs_running") {
            if (!(fields >> parsed.procs_running)) return false;
        } else if (key == "procs_blocked") {
            if (!(fields >> parsed.procs_blocked)) return false;
        }
    }
    if (!has_cpu) {
        return false;
    }
    out = parsed;
    return true;
}

bool read_global_stat(GlobalStat& out) {
    std::string data;
    return read_file("/proc/stat", data) && parse_global_stat(data, out);
}

bool parse_vmstat(const std::string& data, VmStat& out) {
    VmStat parsed;
    std::istringstream lines(data);
    std::string key;
    std::uint64_t value = 0;
    while (lines >> key >> value) {
        if (key == "pgfault") {
            parsed.pgfault = value;
        } else if (key == "pgmajfault") {
            parsed.pgmajfault = value;
        } else if (key == "pswpin") {
            parsed.pswpin = value;
        } else if (key == "pswpout") {
            parsed.pswpout = value;
        }
    }
    out = parsed;
    return true;
}

bool read_vmstat(VmStat& out) {
    std::string data;
    return read_file("/proc/vmstat", data) && parse_vmstat(data, out);
}

}  // namespace memtest::metrics

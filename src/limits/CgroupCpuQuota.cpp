#include "limits/CgroupCpuQuota.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include <unistd.h>

namespace memtest::limits {
namespace {

constexpr const char* kCgroupRoot = "/sys/fs/cgroup";
constexpr int kPeriodUsec = 100000;

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool write_file(const std::string& path, const std::string& value,
                std::string* detail) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        if (detail != nullptr) {
            *detail = "no se pudo escribir " + path + ": " +
                      std::strerror(errno);
        }
        return false;
    }
    out << value;
    if (!out) {
        if (detail != nullptr) {
            *detail = "fallo al escribir " + path;
        }
        return false;
    }
    return true;
}

bool has_word(const std::string& text, const std::string& word) {
    std::istringstream fields(text);
    std::string token;
    while (fields >> token) {
        if (token == word) {
            return true;
        }
    }
    return false;
}

std::string current_cgroup_path() {
    std::string data;
    if (!read_file("/proc/self/cgroup", data)) {
        return {};
    }
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        // En cgroup v2 la entrada es "0::/ruta".
        if (line.rfind("0::", 0) == 0) {
            return line.substr(3);
        }
    }
    return {};
}

bool has_cpu_controller(const std::string& dir) {
    std::string controllers;
    return read_file(dir + "/cgroup.controllers", controllers) &&
           has_word(controllers, "cpu");
}

bool enable_cpu_controller(const std::string& dir, std::string* detail) {
    std::string subtree;
    if (read_file(dir + "/cgroup.subtree_control", subtree) &&
        has_word(subtree, "cpu")) {
        return true;
    }
    if (!write_file(dir + "/cgroup.subtree_control", "+cpu\n", detail)) {
        return false;
    }
    if (!read_file(dir + "/cgroup.subtree_control", subtree) ||
        !has_word(subtree, "cpu")) {
        if (detail != nullptr) {
            *detail = "el kernel no habilito cpu en " + dir;
        }
        return false;
    }
    return true;
}

// Busca el ancestro mas cercano que tenga el controlador cpu disponible y
// cuyo subtree_control se pueda escribir. Se empieza por el padre del cgroup
// actual para no intentar habilitar controladores en un cgroup con procesos.
std::string find_parent(std::string* detail) {
    const std::string current = current_cgroup_path();
    if (current.empty()) {
        if (detail != nullptr) {
            *detail = "no se pudo leer /proc/self/cgroup";
        }
        return {};
    }

    std::filesystem::path candidate =
        std::filesystem::path(current).parent_path();
    while (!candidate.empty()) {
        const std::string dir =
            std::string(kCgroupRoot) + candidate.generic_string();
        if (has_cpu_controller(dir) &&
            ::access(dir.c_str(), W_OK) == 0) {
            std::string local_detail;
            if (enable_cpu_controller(dir, &local_detail)) {
                return dir;
            }
            if (detail != nullptr && detail->empty()) {
                *detail = local_detail;
            }
        }
        if (candidate == candidate.root_path()) {
            break;
        }
        candidate = candidate.parent_path();
    }

    if (detail != nullptr && detail->empty()) {
        *detail = "no hay un ancestro de cgroup con cpu delegado";
    }
    return {};
}

bool create_child(const std::string& parent, const std::string& name,
                  int percent, std::string& child, std::string* detail) {
    const std::filesystem::path path =
        std::filesystem::path(parent) / name;
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        std::filesystem::remove(path, error);
        if (error) {
            if (detail != nullptr) {
                *detail = "cgroup de prueba ya existe: " + path.string();
            }
            return false;
        }
    }
    if (!std::filesystem::create_directory(path, error) || error) {
        if (detail != nullptr) {
            *detail = "no se pudo crear " + path.string() + ": " +
                      error.message();
        }
        return false;
    }

    const int quota = std::clamp(percent, 1, 100) * 1000;
    const std::string max_line =
        std::to_string(quota) + " " + std::to_string(kPeriodUsec) + "\n";
    if (!write_file(path.string() + "/cpu.max", max_line, detail)) {
        std::filesystem::remove(path, error);
        return false;
    }

    child = path.string();
    return true;
}

}  // namespace

CgroupCpuQuota::~CgroupCpuQuota() { cleanup(); }

bool CgroupCpuQuota::supported(std::string* detail) {
    const std::string parent = find_parent(detail);
    if (parent.empty()) {
        return false;
    }

    std::string child;
    const std::string name = "mem-test-probe-" + std::to_string(::getpid());
    if (!create_child(parent, name, 50, child, detail)) {
        return false;
    }
    std::error_code error;
    std::filesystem::remove(child, error);
    return true;
}

bool CgroupCpuQuota::apply(int percent, std::string* detail) {
    cleanup();

    const std::string current = current_cgroup_path();
    if (current.empty()) {
        if (detail != nullptr) {
            *detail = "no se pudo leer /proc/self/cgroup";
        }
        return false;
    }

    const std::string parent = find_parent(detail);
    if (parent.empty()) {
        return false;
    }

    const std::string name = "mem-test-" + std::to_string(::getpid());
    if (!create_child(parent, name, percent, child_path_, detail)) {
        return false;
    }

    original_path_ = std::string(kCgroupRoot) + current;
    const std::string pid = std::to_string(::getpid()) + "\n";
    if (!write_file(child_path_ + "/cgroup.procs", pid, detail)) {
        std::error_code error;
        std::filesystem::remove(child_path_, error);
        child_path_.clear();
        return false;
    }

    applied_ = true;
    return true;
}

bool CgroupCpuQuota::read_stats(std::uint64_t& nr_throttled,
                                std::uint64_t& throttled_usec) const {
    if (!applied_ || child_path_.empty()) {
        return false;
    }
    std::string data;
    if (!read_file(child_path_ + "/cpu.stat", data)) {
        return false;
    }
    std::istringstream lines(data);
    std::string key;
    std::uint64_t value = 0;
    while (lines >> key >> value) {
        if (key == "nr_throttled") {
            nr_throttled = value;
        } else if (key == "throttled_usec") {
            throttled_usec = value;
        }
    }
    return true;
}

void CgroupCpuQuota::cleanup() {
    if (!applied_) {
        return;
    }
    const std::string pid = std::to_string(::getpid()) + "\n";
    std::string ignored;
    (void)write_file(original_path_ + "/cgroup.procs", pid, &ignored);
    std::error_code error;
    std::filesystem::remove(child_path_, error);
    child_path_.clear();
    applied_ = false;
}

}  // namespace memtest::limits

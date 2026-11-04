#include <iostream>
#include <map>
#include <string>

#include "Suites.hpp"

int main(int argc, char** argv) {
    const std::map<std::string, memtest::test::Suite (*)()> suites = {
        {"domain", memtest::test::domain_suite},
        {"fake_controller", memtest::test::fake_controller_suite},
        {"main_scene", memtest::test::main_scene_suite},
        {"worker_pool", memtest::test::worker_pool_suite},
        {"workload_sync", memtest::test::workload_sync_suite},
        {"worker_protocol", memtest::test::worker_protocol_suite},
        {"process_supervisor", memtest::test::process_supervisor_suite},
        {"experiment_controller", memtest::test::experiment_controller_suite},
    };

    if (argc != 2) {
        std::cerr << "Uso: memtest_tests <suite>\nSuites:";
        for (const auto& entry : suites) {
            std::cerr << ' ' << entry.first;
        }
        std::cerr << '\n';
        return 2;
    }

    const std::string name = argv[1];
    const auto it = suites.find(name);
    if (it == suites.end()) {
        std::cerr << "Suite desconocida: " << name << '\n';
        return 2;
    }

    return memtest::test::run_suite(name, it->second());
}

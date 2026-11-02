#include <iostream>
#include <map>
#include <string>

#include "Suites.hpp"

int main(int argc, char** argv) {
    const std::map<std::string, memtest::test::Suite (*)()> suites = {
        {"domain", memtest::test::domain_suite},
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

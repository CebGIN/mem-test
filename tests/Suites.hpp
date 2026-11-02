#ifndef MEMTEST_TESTS_SUITES_HPP
#define MEMTEST_TESTS_SUITES_HPP

#include "TestHarness.hpp"

namespace memtest::test {

// Cada archivo de pruebas expone su suite; tests/main.cpp la selecciona por
// argv para que CTest registre un test por suite.
Suite domain_suite();
Suite fake_controller_suite();
Suite main_scene_suite();

}  // namespace memtest::test

#endif  // MEMTEST_TESTS_SUITES_HPP

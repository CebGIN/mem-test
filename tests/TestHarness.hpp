// PROVISIONAL (WP1): arnés mínimo CHECK/CHECK_EQ. El del Lead (WP0) lo reemplaza;
// se mantienen los mismos nombres de macro.
#ifndef MEMTEST_TESTS_TESTHARNESS_HPP
#define MEMTEST_TESTS_TESTHARNESS_HPP

#include <iostream>

namespace memtest_test {
inline int& failures() {
    static int count = 0;
    return count;
}
}  // namespace memtest_test

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++memtest_test::failures();                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK falló: "     \
                      << #cond << "\n";                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        const auto a_ = (actual);                                            \
        const auto e_ = (expected);                                          \
        if (!(a_ == e_)) {                                                   \
            ++memtest_test::failures();                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK_EQ falló: "  \
                      << #actual << " = " << a_ << ", esperado " << e_       \
                      << "\n";                                               \
        }                                                                    \
    } while (0)

#define TEST_RESULT() (memtest_test::failures() == 0 ? 0 : 1)

#endif  // MEMTEST_TESTS_TESTHARNESS_HPP

#ifndef MEMTEST_TESTS_TESTHARNESS_HPP
#define MEMTEST_TESTS_TESTHARNESS_HPP

#include <cstddef>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace memtest::test {

namespace detail {

// Los enums del dominio no tienen operator<<; en ese caso se imprime una marca
// en vez de romper la compilación del test.
template<typename T, typename = void>
struct is_streamable : std::false_type {};

template<typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>()
                                            << std::declval<const T&>())>> : std::true_type {};

template<typename T>
void print_value(std::ostream& os, const T& value) {
    if constexpr (is_streamable<T>::value) {
        os << value;
    } else {
        os << "<valor no imprimible>";
    }
}

}  // namespace detail

// Contador global para los tests standalone que no usan Context (los de WP1).
// La misma cabecera sirve para CHECK(cond) y CHECK(ctx, cond).
inline int& global_failures() {
    static int count = 0;
    return count;
}

inline void check_global(bool condition, const char* expression, const char* file,
                         int line) {
    if (condition) {
        return;
    }
    ++global_failures();
    std::cerr << file << ":" << line << ": CHECK falló: " << expression << '\n';
}

template<typename A, typename B>
void check_eq_global(const A& actual, const B& expected,
                     const char* actual_expression, const char* expected_expression,
                     const char* file, int line) {
    if (actual == expected) {
        return;
    }
    ++global_failures();
    std::cerr << file << ":" << line << ": CHECK_EQ falló: " << actual_expression
              << " == " << expected_expression << "\n  obtenido: ";
    detail::print_value(std::cerr, actual);
    std::cerr << "\n  esperado: ";
    detail::print_value(std::cerr, expected);
    std::cerr << '\n';
}

// Contexto de una prueba: acumula fallos y los imprime con archivo y línea.
// No hay framework externo (Arquitectura.md §16).
class Context {
public:
    explicit Context(std::string case_name) : case_name_(std::move(case_name)) {}

    void check(bool condition, const char* expression, const char* file, int line) {
        if (condition) {
            return;
        }
        ++failures_;
        std::cerr << file << ":" << line << ": CHECK falló: " << expression << '\n';
    }

    template<typename A, typename B>
    void check_eq(const A& actual, const B& expected, const char* actual_expression,
                  const char* expected_expression, const char* file, int line) {
        if (actual == expected) {
            return;
        }
        ++failures_;
        std::cerr << file << ":" << line << ": CHECK_EQ falló: " << actual_expression
                  << " == " << expected_expression << "\n  obtenido: ";
        detail::print_value(std::cerr, actual);
        std::cerr << "\n  esperado: ";
        detail::print_value(std::cerr, expected);
        std::cerr << '\n';
    }

    int failures() const { return failures_; }
    const std::string& case_name() const { return case_name_; }

private:
    std::string case_name_;
    int failures_ = 0;
};

using TestFn = void (*)(Context&);

struct Case {
    const char* name;
    TestFn fn;
};

using Suite = std::vector<Case>;

// Corre todos los casos y devuelve 0 si no hubo fallos, 1 en caso contrario.
inline int run_suite(const std::string& suite_name, const Suite& cases) {
    int failures = 0;
    for (const Case& test_case : cases) {
        Context context(test_case.name);
        test_case.fn(context);
        if (context.failures() == 0) {
            std::cout << "[ OK ] " << suite_name << " · " << test_case.name << '\n';
        } else {
            std::cout << "[FALLA] " << suite_name << " · " << test_case.name << " ("
                      << context.failures() << " fallos)\n";
            failures += context.failures();
        }
    }
    if (failures != 0) {
        std::cerr << suite_name << ": " << failures << " comprobaciones fallidas\n";
        return 1;
    }
    return 0;
}

}  // namespace memtest::test

// Las macros aceptan dos estilos:
//   CHECK(cond)             CHECK_EQ(actual, expected)        (tests WP1)
//   CHECK(context, cond)    CHECK_EQ(context, actual, exp.)   (suites WP0/WP2)
#define MEMTEST_CHECK_SELECT(_1, _2, NAME, ...) NAME
#define MEMTEST_CHECK_EQ_SELECT(_1, _2, _3, NAME, ...) NAME

#define CHECK(...) \
    MEMTEST_CHECK_SELECT(__VA_ARGS__, MEMTEST_CHECK_CTX, MEMTEST_CHECK_ONE)(__VA_ARGS__)

#define CHECK_EQ(...) \
    MEMTEST_CHECK_EQ_SELECT(__VA_ARGS__, MEMTEST_CHECK_EQ_CTX, \
                            MEMTEST_CHECK_EQ_ONE)(__VA_ARGS__)

#define MEMTEST_CHECK_CTX(context, condition) \
    (context).check((condition), #condition, __FILE__, __LINE__)

#define MEMTEST_CHECK_ONE(condition) \
    memtest::test::check_global((condition), #condition, __FILE__, __LINE__)

#define MEMTEST_CHECK_EQ_CTX(context, actual, expected) \
    (context).check_eq((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define MEMTEST_CHECK_EQ_ONE(actual, expected) \
    memtest::test::check_eq_global((actual), (expected), #actual, #expected, \
                                   __FILE__, __LINE__)

#define TEST_RESULT() (memtest::test::global_failures() == 0 ? 0 : 1)

#endif  // MEMTEST_TESTS_TESTHARNESS_HPP

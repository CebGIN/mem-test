#ifndef MEMTEST_CORE_RESULT_STORE_HPP
#define MEMTEST_CORE_RESULT_STORE_HPP

#include "domain/Types.hpp"
#include <vector>
#include <mutex>
#include <cstddef>

namespace memtest::core {

// Clase encargada de almacenar y administrar el historial de resultados en memoria.
class ResultStore {
public:
    ResultStore() = default;

    // Agrega un resultado al historial
    void add_result(const domain::ExperimentResult& result);

    // Devuelve todos los resultados guardados
    std::vector<domain::ExperimentResult> get_all_results() const;

    // Limpia el historial completo
    void clear();

    // Devuelve la cantidad total de resultados almacenados
    std::size_t count() const;

private:
    mutable std::mutex mutex_;
    std::vector<domain::ExperimentResult> results_;
};

} // namespace memtest::core

#endif // MEMTEST_CORE_RESULT_STORE_HPP
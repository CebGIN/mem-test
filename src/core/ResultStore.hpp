#ifndef MEMTEST_CORE_RESULTSTORE_HPP
#define MEMTEST_CORE_RESULTSTORE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "domain/Types.hpp"

namespace memtest {

// Historial de la sesión en memoria (Arquitectura.md §14). No conoce la UI ni
// escribe archivos: los controladores lo protegen con su propio mutex y
// CsvExporter lo lee para exportar.
class ResultStore {
public:
    // Guarda una ejecución al final del historial. El orden es cronológico.
    void add(ExperimentResult result);

    const std::vector<ExperimentResult>& results() const { return results_; }

    // Copia inmutable para que la UI y la exportación no compartan estado.
    std::vector<ExperimentResult> snapshot() const { return results_; }

    // Devuelve nullptr si no existe esa ejecución.
    const ExperimentResult* find(const std::string& run_id) const;

    std::size_t size() const { return results_.size(); }
    bool empty() const { return results_.empty(); }
    void clear() { results_.clear(); }

private:
    std::vector<ExperimentResult> results_;
};

}  // namespace memtest

#endif  // MEMTEST_CORE_RESULTSTORE_HPP

#ifndef MEMTEST_CORE_CSV_EXPORTER_HPP
#define MEMTEST_CORE_CSV_EXPORTER_HPP

#include "domain/Types.hpp"
#include <string>
#include <vector>

namespace memtest::core {

class CsvExporter {
public:
    // recibe la ruta del archivo  y la lista de resultados que guardamos en el ResultStore.
    static bool export_to_csv(const std::string& filepath, 
                              const std::vector<domain::ExperimentResult>& results);
};
} 

#endif 
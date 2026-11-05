#ifndef MEMTEST_CORE_CSVEXPORTER_HPP
#define MEMTEST_CORE_CSVEXPORTER_HPP

#include <filesystem>
#include <string>
#include <vector>

#include "domain/Types.hpp"

namespace memtest {

// Archivos escritos por una exportación. `summary` siempre está presente si
// write() devuelve true; `details` tiene un archivo por ejecución con serie.
struct CsvExportPaths {
    std::filesystem::path summary;
    std::vector<std::filesystem::path> details;
};

// Exporta el historial a CSV (Arquitectura.md §14): un resumen con una fila por
// ejecución y un detalle por ejecución. Crea el directorio si falta. No lanza:
// ante cualquier fallo devuelve false y escribe el motivo en `error`.
class CsvExporter {
public:
    static bool write(const std::vector<ExperimentResult>& results,
                      const std::filesystem::path& directory,
                      CsvExportPaths* paths,
                      std::string* error);
};

}  // namespace memtest

#endif  // MEMTEST_CORE_CSVEXPORTER_HPP

#include "core/CsvExporter.hpp"
#include <fstream> // Librería para crear y escribir archivos de texto

namespace memtest::core {

bool CsvExporter::export_to_csv(const std::string& filepath, 
                              const std::vector<domain::ExperimentResult>& results) {
    
    // Abre el archivo en modo escritura y borra lo anterior si ya existía
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    
    // Si el archivo no se pudo abrir, devuelve false
    if (!file.is_open()) {
        return false;
    }

    // Escribimos la CABECERA del archivo 
    file << "ExperimentID,WorkloadType,DurationMs,PeakMemoryBytes,CpuUsagePct,Status\n";

    // Recorremos cada resultado guardado y escribimos una línea por cada uno
    for (const auto& res : results) {
        file << res.id << ","
             << static_cast<int>(res.config.workload_type) << ","
             << res.duration_ms << ","
             << res.peak_memory_bytes << ","
             << res.cpu_usage_pct << ","
             << static_cast<int>(res.status) << "\n";
    }

    // Cerramos el archivo para liberar el recurso del sistema operativo
    file.close();
    
    return true;
}

} 
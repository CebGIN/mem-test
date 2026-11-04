#ifndef MEMTEST_CORE_RESULT_STORE_HPP
#define MEMTEST_CORE_RESULT_STORE_HPP

#include "domain/Types.hpp" //datos base del proyecto
#include <vector>
#include <mutex> //evita problemas al guardar vaios hilos

namespace memtest::core {

class ResultStore {
public:
    ResultStore() = default; //constructor

    // Agrega un nuevo resultado de experimento al historial
    void add_result(const domain::ExperimentResult& result) {
        std::lock_guard<std::mutex> lock(mutex_); //bloquea para que sea seguro con hilos
        results_.push_back(result); //lo anade al final del vector
    }

    // Obtiene todos los resultados almacenados
    std::vector<domain::ExperimentResult> get_all_results() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return results_;
    }

    // Limpia el historial si el usuario lo solicita
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        results_.clear();
    }

    // Devuelve la cantidad de resultados guardados
    std::size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return results_.size();
    }
    

private: //potege los datos
    mutable std::mutex mutex_;
    std::vector<domain::ExperimentResult> results_;
};

} 

#endif 
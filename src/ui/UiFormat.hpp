#ifndef MEMTEST_UI_UIFORMAT_HPP
#define MEMTEST_UI_UIFORMAT_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "domain/Types.hpp"

namespace memtest::ui {

inline constexpr std::uint64_t kMiB = 1024ull * 1024ull;
inline constexpr std::uint64_t kGiB = 1024ull * kMiB;

// Formato con unidades visibles (Arquitectura.md §13).
std::string mib(std::uint64_t bytes);
std::string gib(std::uint64_t bytes);
std::string seconds_text(double seconds);
std::string duration_text(std::chrono::milliseconds duration);

std::string state_text(RunState state);
std::string outcome_text(RunOutcome outcome);
std::string workload_text(WorkloadKind kind);
std::string worker_mode_text(WorkerMode mode);
std::string sync_mode_text(SyncMode mode);
std::string memory_limit_text(MemoryLimitKind kind);
std::string limit_status_text(LimitStatus status);

// Una fila compacta del historial, con las unidades ya incluidas.
std::string history_row(const ExperimentResult& result);

// Resumen de límites de una ejecución para la barra de estado.
std::string limits_summary(const ExperimentResult& result);

// Opciones de los ListView del formulario, en el mismo orden que los enums.
std::vector<std::string> workload_items();
std::vector<std::string> worker_mode_items();
std::vector<std::string> sync_mode_items();
std::vector<std::string> memory_limit_items();

}  // namespace memtest::ui

#endif  // MEMTEST_UI_UIFORMAT_HPP

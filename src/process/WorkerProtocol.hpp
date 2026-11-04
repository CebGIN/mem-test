#ifndef MEMTEST_PROCESS_WORKERPROTOCOL_HPP
#define MEMTEST_PROCESS_WORKERPROTOCOL_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "domain/Types.hpp"
#include "process/IProcessSupervisor.hpp"

namespace memtest {

// El protocolo es una línea por mensaje. Las líneas no pueden superar este
// tamaño (Arquitectura.md §7.2).
inline constexpr std::size_t kMaxProtocolLine = 4096;

// Configuración padre -> worker (una sola línea).
std::string encode_config(const ExperimentConfig& config);
bool parse_config(std::string_view line, ExperimentConfig& out,
                  std::string* error = nullptr);

// Eventos worker -> padre.
std::string encode_event(const WorkerEvent& event);
std::optional<WorkerEvent> parse_event(std::string_view line,
                                       std::string* error = nullptr);

std::string limit_status_name(LimitStatus status);
std::optional<LimitStatus> parse_limit_status(std::string_view text);

std::string run_outcome_name(RunOutcome outcome);
std::optional<RunOutcome> parse_run_outcome(std::string_view text);

}  // namespace memtest

#endif  // MEMTEST_PROCESS_WORKERPROTOCOL_HPP

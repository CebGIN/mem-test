#ifndef MEMTEST_PROCESS_IPROCESSSUPERVISOR_HPP
#define MEMTEST_PROCESS_IPROCESSSUPERVISOR_HPP

#include <sys/types.h>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "domain/Types.hpp"

namespace memtest {

// Un evento del protocolo clave=valor que escribe el worker por stdout
// (Arquitectura.md §7.2).
struct WorkerEvent {
    enum class Type { Hello, Ready, Limits, Progress, Result, Error };
    Type type;
    std::map<std::string, std::string> fields;
};

struct ProcessExit {
    int exit_code = -1;
    int term_signal = 0;
    bool timed_out = false;
};

// Lanza, controla y cosecha el proceso hijo mem-test-worker. El padre solo
// escribe la configuración por stdin y lee eventos por stdout; al hijo se lo
// controla con señales.
class IProcessSupervisor {
public:
    virtual ~IProcessSupervisor() = default;
    virtual bool spawn(const ExperimentConfig& config, std::string* error) = 0;
    virtual bool running() const = 0;
    virtual pid_t pid() const = 0;
    virtual std::vector<WorkerEvent> poll_events() = 0;
    virtual void request_stop() = 0;
    virtual std::optional<ProcessExit> wait_for_exit(std::chrono::milliseconds timeout) = 0;
};

}  // namespace memtest

#endif  // MEMTEST_PROCESS_IPROCESSSUPERVISOR_HPP

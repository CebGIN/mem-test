#ifndef MEMTEST_PROCESS_POSIXPROCESSSUPERVISOR_HPP
#define MEMTEST_PROCESS_POSIXPROCESSSUPERVISOR_HPP

#include <string>

#include "process/IProcessSupervisor.hpp"

namespace memtest {

// Implementación Linux del supervisor: posix_spawn, pipes, grupo de procesos y
// señales (Arquitectura.md §7).
class PosixProcessSupervisor final : public IProcessSupervisor {
public:
    // Si `worker_path` queda vacío se resuelve como el `mem-test-worker` que
    // está junto al ejecutable actual (o MEMTEST_WORKER).
    explicit PosixProcessSupervisor(std::string worker_path = {});
    ~PosixProcessSupervisor() override;

    PosixProcessSupervisor(const PosixProcessSupervisor&) = delete;
    PosixProcessSupervisor& operator=(const PosixProcessSupervisor&) = delete;

    bool spawn(const ExperimentConfig& config, std::string* error) override;
    bool running() const override;
    pid_t pid() const override;
    std::vector<WorkerEvent> poll_events() override;
    void request_stop() override;
    std::optional<ProcessExit> wait_for_exit(
        std::chrono::milliseconds timeout) override;

private:
    void close_pipes();
    void read_available();

    std::string worker_path_;
    pid_t pid_ = -1;
    int stdout_fd_ = -1;
    int stderr_fd_ = -1;
    std::string stdout_buffer_;
    std::string stderr_buffer_;
    bool running_ = false;
    bool stop_requested_ = false;
};

}  // namespace memtest

#endif  // MEMTEST_PROCESS_POSIXPROCESSSUPERVISOR_HPP

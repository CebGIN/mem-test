#include "process/PosixProcessSupervisor.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "process/WorkerProtocol.hpp"

extern char** environ;

namespace memtest {
namespace {

std::string resolve_worker_path() {
    if (const char* env = std::getenv("MEMTEST_WORKER");
        env != nullptr && *env != '\0') {
        return env;
    }

    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer,
                                      sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        const std::string self(buffer);
        const std::size_t slash = self.find_last_of('/');
        if (slash != std::string::npos) {
            return self.substr(0, slash + 1) + "mem-test-worker";
        }
    }
    return "mem-test-worker";
}

void set_error(std::string* error, const std::string& text) {
    if (error != nullptr) {
        *error = text;
    }
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

}  // namespace

PosixProcessSupervisor::PosixProcessSupervisor(std::string worker_path)
    : worker_path_(worker_path.empty() ? resolve_worker_path()
                                       : std::move(worker_path)) {}

PosixProcessSupervisor::~PosixProcessSupervisor() {
    if (pid_ > 0) {
        ::kill(-pid_, SIGKILL);
        int status = 0;
        (void)::waitpid(pid_, &status, 0);
        pid_ = -1;
    }
    close_pipes();
}

bool PosixProcessSupervisor::spawn(const ExperimentConfig& config,
                                   std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (running_ || pid_ > 0) {
        set_error(error, "ya hay un worker en ejecucion");
        return false;
    }
    close_pipes();

    if (::access(worker_path_.c_str(), X_OK) != 0) {
        set_error(error, "no se encontro el worker '" + worker_path_ +
                             "': " + std::strerror(errno));
        return false;
    }

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    auto close_all = [&] {
        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
    };

    if (::pipe(stdin_pipe) != 0 || ::pipe(stdout_pipe) != 0 ||
        ::pipe(stderr_pipe) != 0) {
        set_error(error, std::string("pipe fallo: ") + std::strerror(errno));
        close_all();
        return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);

    posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);

    if (stdin_pipe[0] != STDIN_FILENO) {
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
    }
    if (stdout_pipe[1] != STDOUT_FILENO) {
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    }
    if (stderr_pipe[1] != STDERR_FILENO) {
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
    }
    posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);

    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);

    char* argv[] = {const_cast<char*>(worker_path_.c_str()), nullptr};
    pid_t child = -1;
    const int spawn_error =
        posix_spawn(&child, worker_path_.c_str(), &actions, &attributes,
                    argv, environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);

    if (spawn_error != 0) {
        set_error(error, std::string("posix_spawn fallo: ") +
                             std::strerror(spawn_error));
        close_all();
        return false;
    }

    close_fd(stdin_pipe[0]);
    close_fd(stdout_pipe[1]);
    close_fd(stderr_pipe[1]);

    const int out_flags = ::fcntl(stdout_pipe[0], F_GETFL, 0);
    if (out_flags >= 0) {
        (void)::fcntl(stdout_pipe[0], F_SETFL, out_flags | O_NONBLOCK);
    }
    const int err_flags = ::fcntl(stderr_pipe[0], F_GETFL, 0);
    if (err_flags >= 0) {
        (void)::fcntl(stderr_pipe[0], F_SETFL, err_flags | O_NONBLOCK);
    }

    const std::string line = encode_config(config) + "\n";
    std::size_t written = 0;
    while (written < line.size()) {
        const ssize_t count = ::write(stdin_pipe[1], line.data() + written,
                                      line.size() - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close_fd(stdin_pipe[1]);

    if (written != line.size()) {
        set_error(error, "no se pudo enviar la configuracion al worker");
        ::kill(-child, SIGKILL);
        int status = 0;
        (void)::waitpid(child, &status, 0);
        close_fd(stdout_pipe[0]);
        close_fd(stderr_pipe[0]);
        return false;
    }

    pid_ = child;
    stdout_fd_ = stdout_pipe[0];
    stderr_fd_ = stderr_pipe[0];
    stdout_buffer_.clear();
    stderr_buffer_.clear();
    running_ = true;
    stop_requested_ = false;
    return true;
}

bool PosixProcessSupervisor::running() const {
    return running_ && pid_ > 0;
}

pid_t PosixProcessSupervisor::pid() const { return pid_; }

void PosixProcessSupervisor::read_available() {
    auto read_into = [](int fd, std::string& buffer) {
        if (fd < 0) {
            return;
        }
        char chunk[4096];
        for (;;) {
            const ssize_t count = ::read(fd, chunk, sizeof(chunk));
            if (count > 0) {
                buffer.append(chunk, static_cast<std::size_t>(count));
            } else if (count == 0) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                break;
            }
        }
    };

    read_into(stdout_fd_, stdout_buffer_);
    read_into(stderr_fd_, stderr_buffer_);
    if (stderr_buffer_.size() > 4096) {
        stderr_buffer_.erase(0, stderr_buffer_.size() - 4096);
    }
}

std::vector<WorkerEvent> PosixProcessSupervisor::poll_events() {
    std::vector<WorkerEvent> events;
    read_available();

    std::size_t newline = 0;
    while ((newline = stdout_buffer_.find('\n')) != std::string::npos) {
        std::string line = stdout_buffer_.substr(0, newline);
        stdout_buffer_.erase(0, newline + 1);
        if (line.empty()) {
            continue;
        }
        std::string error;
        auto event = parse_event(line, &error);
        if (event) {
            events.push_back(std::move(*event));
        } else {
            WorkerEvent malformed;
            malformed.type = WorkerEvent::Type::Error;
            malformed.fields["code"] = "protocol";
            malformed.fields["detail"] =
                error.empty() ? "evento malformado" : error;
            events.push_back(std::move(malformed));
        }
    }
    return events;
}

void PosixProcessSupervisor::request_stop() {
    if (pid_ <= 0 || !running_) {
        return;
    }
    stop_requested_ = true;
    if (::kill(-pid_, SIGTERM) != 0) {
        (void)::kill(pid_, SIGTERM);
    }
}

std::optional<ProcessExit> PosixProcessSupervisor::wait_for_exit(
    std::chrono::milliseconds timeout) {
    if (pid_ <= 0) {
        return std::nullopt;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        int status = 0;
        const pid_t result = ::waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            ProcessExit exit;
            if (WIFEXITED(status)) {
                exit.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit.term_signal = WTERMSIG(status);
            }
            running_ = false;
            pid_ = -1;
            return exit;
        }
        if (result < 0 && errno != EINTR) {
            running_ = false;
            pid_ = -1;
            ProcessExit exit;
            exit.exit_code = -1;
            return exit;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void PosixProcessSupervisor::close_pipes() {
    close_fd(stdout_fd_);
    close_fd(stderr_fd_);
    stdout_buffer_.clear();
    stderr_buffer_.clear();
}

}  // namespace memtest

#include <chrono>
#include <csignal>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "Suites.hpp"
#include "process/PosixProcessSupervisor.hpp"

namespace memtest::test {
namespace {

using namespace std::chrono_literals;

std::string worker_path() {
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return "mem-test-worker";
    }
    return (self.parent_path() / "mem-test-worker").string();
}

bool has_event(const std::vector<WorkerEvent>& events, WorkerEvent::Type type) {
    for (const WorkerEvent& event : events) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

bool wait_for_ready(PosixProcessSupervisor& supervisor,
                    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        for (const WorkerEvent& event : supervisor.poll_events()) {
            if (event.type == WorkerEvent::Type::Ready) {
                return true;
            }
        }
        if (supervisor.wait_for_exit(20ms)) {
            return false;
        }
    }
    return false;
}

void completes_and_reports_protocol(Context& context) {
    PosixProcessSupervisor supervisor(worker_path());
    ExperimentConfig config;
    config.threads = 1;
    config.work_units = 1000;
    config.wall_timeout_ms = 5000;

    std::string error;
    CHECK(context, supervisor.spawn(config, &error));
    CHECK(context, error.empty());
    CHECK(context, supervisor.running());
    CHECK(context, supervisor.pid() > 0);

    std::vector<WorkerEvent> events;
    std::optional<ProcessExit> exit;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        for (const WorkerEvent& event : supervisor.poll_events()) {
            events.push_back(event);
        }
        exit = supervisor.wait_for_exit(50ms);
        if (exit) {
            break;
        }
    }
    for (const WorkerEvent& event : supervisor.poll_events()) {
        events.push_back(event);
    }

    CHECK(context, exit.has_value());
    if (exit) {
        CHECK_EQ(context, exit->exit_code, 0);
        CHECK_EQ(context, exit->term_signal, 0);
    }
    CHECK(context, !supervisor.running());
    CHECK(context, has_event(events, WorkerEvent::Type::Hello));
    CHECK(context, has_event(events, WorkerEvent::Type::Ready));
    CHECK(context, has_event(events, WorkerEvent::Type::Limits));
    CHECK(context, has_event(events, WorkerEvent::Type::Result));
    CHECK(context, has_event(events, WorkerEvent::Type::End));
}

void stops_with_sigterm(Context& context) {
    PosixProcessSupervisor supervisor(worker_path());
    ExperimentConfig config;
    config.threads = 1;
    config.work_units = 1000000000ull;
    config.wall_timeout_ms = 120000;

    std::string error;
    CHECK(context, supervisor.spawn(config, &error));
    CHECK(context, wait_for_ready(supervisor, 3000ms));

    supervisor.request_stop();
    const auto exit = supervisor.wait_for_exit(3000ms);
    CHECK(context, exit.has_value());
    if (exit) {
        CHECK_EQ(context, exit->exit_code, 12);
        CHECK_EQ(context, exit->term_signal, 0);
    }
    CHECK(context, !supervisor.running());
}

void external_sigkill_is_visible(Context& context) {
    PosixProcessSupervisor supervisor(worker_path());
    ExperimentConfig config;
    config.threads = 1;
    config.work_units = 1000000000ull;
    config.wall_timeout_ms = 120000;

    std::string error;
    CHECK(context, supervisor.spawn(config, &error));
    CHECK(context, wait_for_ready(supervisor, 3000ms));

    const pid_t child = supervisor.pid();
    CHECK(context, child > 0);
    if (child > 0) {
        CHECK(context, ::kill(child, SIGKILL) == 0);
    }
    const auto exit = supervisor.wait_for_exit(3000ms);
    CHECK(context, exit.has_value());
    if (exit) {
        CHECK_EQ(context, exit->term_signal, SIGKILL);
        CHECK_EQ(context, exit->exit_code, -1);
    }
}

}  // namespace

Suite process_supervisor_suite() {
    return {
        {"completa y reporta protocolo", completes_and_reports_protocol},
        {"SIGTERM detiene el worker", stops_with_sigterm},
        {"SIGKILL externo es visible", external_sigkill_is_visible},
    };
}

}  // namespace memtest::test

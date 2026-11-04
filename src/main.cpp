#include <memory>
#include <utility>

#include <guayaba/Guayaba.hpp>

#include "app/ExperimentController.hpp"
#include "core/SystemInfo.hpp"
#include "domain/Validation.hpp"
#include "metrics/MetricsSourceFactory.hpp"
#include "process/PosixProcessSupervisor.hpp"
#include "ui/MainScene.hpp"

int main() {
    // WP4: la TUI usa el controlador real. El worker se lanza como proceso hijo
    // y el padre muestrea /proc/<pid> sin compartir memoria con la carga.
    const memtest::SystemInfo info = memtest::read_system_info();
    auto controller = std::make_unique<memtest::ExperimentController>(
        memtest::make_metrics_source(),
        std::make_unique<memtest::PosixProcessSupervisor>(), info);

    auto scene = memtest::ui::build_main_scene(*controller, info,
                                               memtest::guardrails_from(info));

    guayaba::SceneManager& manager = guayaba::SceneManager::get_instance();
    manager.change_scene(std::move(scene));
    manager.set_target_fps(30);
    manager.start_running();
    return 0;
}

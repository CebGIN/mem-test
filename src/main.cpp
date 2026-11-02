#include <memory>

#include <guayaba/Guayaba.hpp>

#include "app/FakeExperimentController.hpp"
#include "core/SystemInfo.hpp"
#include "domain/Validation.hpp"
#include "ui/MainScene.hpp"

int main() {
    // WP0: la TUI corre contra el controlador fake. El supervisor y los workers
    // reales llegan en WP4; el reparto de capas no cambia.
    const memtest::SystemInfo info = memtest::read_system_info();
    auto controller = std::make_unique<memtest::FakeExperimentController>(info);

    auto scene = memtest::ui::build_main_scene(*controller, info, memtest::guardrails_from(info));

    guayaba::SceneManager& manager = guayaba::SceneManager::get_instance();
    manager.change_scene(std::move(scene));
    manager.set_target_fps(30);
    manager.start_running();
    return 0;
}

#include <memory>
#include <string>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "Suites.hpp"
#include "app/FakeExperimentController.hpp"
#include "domain/Validation.hpp"
#include "ui/MainScene.hpp"

namespace memtest::test {
namespace {

using guayaba::ConsoleRenderer;
using guayaba::Control;
using guayaba::InputEvent;
using guayaba::IOSTerminal;
using guayaba::Label;
using guayaba::SceneManager;
using guayaba::TermChar;
using guayaba::Vec2;

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

SystemInfo test_system() {
    SystemInfo info;
    info.total_ram = 8 * 1024 * kMiB;
    info.available_ram = 6 * 1024 * kMiB;
    info.cores = 4;
    return info;
}

// Terminal de captura: permite correr frames sin una TTY real.
class CapturingTerminal : public IOSTerminal {
public:
    void init() override {}
    void restore() override {}
    void set_cursor_visible(bool) override {}
    void set_cursor_position(Vec2) override {}
    void present_buffer(const std::vector<TermChar>& buffer, Vec2 size) override {
        buffer_ = buffer;
        size_ = size;
    }
    Vec2 get_screen_size(Vec2 fallback) const override { return fallback; }
    void poll_events() override {}
    Vec2 get_mouse_position() const override { return {0, 0}; }
    bool is_left_click_pressed() const override { return false; }
    bool is_right_click_pressed() const override { return false; }
    bool is_left_click_just_pressed() const override { return false; }
    bool is_right_click_just_pressed() const override { return false; }
    std::vector<InputEvent> take_input_events() override { return {}; }
    bool was_key_pressed(guayaba::Key) const override { return false; }
    int get_wheel_delta() const override { return 0; }
    std::string get_line_input(Vec2) override { return {}; }

private:
    std::vector<TermChar> buffer_;
    Vec2 size_{0, 0};
};

void test_three_zones_layout(Context& ctx) {
    const SystemInfo info = test_system();
    FakeExperimentController controller(info);

    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{80, 24}, &terminal);
    manager.run_frame(renderer, {});
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() != nullptr) {
        guayaba::NodeRef<Control> form =
            root->get_node_or_null<Control>("screen/layout/body/form");
        guayaba::NodeRef<Control> metrics =
            root->get_node_or_null<Control>("screen/layout/body/right/metrics");
        guayaba::NodeRef<Control> history =
            root->get_node_or_null<Control>("screen/layout/body/right/history");

        CHECK(ctx, form.get() != nullptr);
        CHECK(ctx, metrics.get() != nullptr);
        CHECK(ctx, history.get() != nullptr);

        if (form.get() != nullptr && metrics.get() != nullptr) {
            // Las tres zonas existen y no se solapan a 80x24.
            CHECK(ctx, form->get_rect().size.x > 0 && form->get_rect().size.y > 0);
            CHECK(ctx, metrics->get_rect().size.x > 0 && metrics->get_rect().size.y > 0);
            CHECK(ctx,
                  form->get_rect().pos.x + form->get_rect().size.x < metrics->get_rect().pos.x);
        }
        if (metrics.get() != nullptr && history.get() != nullptr) {
            CHECK(ctx, metrics->get_rect().pos.y + metrics->get_rect().size.y <=
                           history->get_rect().pos.y);
        }

        guayaba::NodeRef<Label> title =
            root->get_node_or_null<Label>("screen/layout/header/title");
        CHECK(ctx, title.get() != nullptr);
        if (title.get() != nullptr) {
            CHECK_EQ(ctx, title->get_text(), std::string("mem-test"));
        }
    }

    // La escena se destruye con su script; el controlador queda vivo para el
    // destructor local.
    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

}  // namespace

Suite main_scene_suite() {
    return {
        {"three_zones_layout", test_three_zones_layout},
    };
}

}  // namespace memtest::test

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "Suites.hpp"
#include "app/ExperimentController.hpp"
#include "app/FakeExperimentController.hpp"
#include "domain/Validation.hpp"
#include "metrics/MetricsSourceFactory.hpp"
#include "process/PosixProcessSupervisor.hpp"
#include "ui/MainScene.hpp"

namespace memtest::test {
namespace {

using namespace std::chrono_literals;

using guayaba::Button;
using guayaba::ConsoleRenderer;
using guayaba::Control;
using guayaba::Graph;
using guayaba::InputEvent;
using guayaba::IOSTerminal;
using guayaba::Key;
using guayaba::Label;
using guayaba::ListView;
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

std::filesystem::path temp_dir(const std::string& tag) {
    static std::atomic<std::uint64_t> counter{1};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("memtest-" + tag + "-" + std::to_string(stamp) + "-" +
            std::to_string(counter.fetch_add(1)));
}

std::string worker_path() {
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return "mem-test-worker";
    }
    return (self.parent_path() / "mem-test-worker").string();
}

InputEvent key_press(Key key) {
    InputEvent event;
    event.type = InputEvent::Type::KEY;
    event.pressed = true;
    event.key = key;
    return event;
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

bool inside(const Control* control, Vec2 screen) {    if (control == nullptr) {
        return false;
    }
    const guayaba::Rect rect = control->get_rect();
    return rect.size.x > 0 && rect.size.y > 0 && rect.pos.x >= 0 && rect.pos.y >= 0 &&
           rect.pos.x + rect.size.x <= screen.x &&
           rect.pos.y + rect.size.y <= screen.y;
}

guayaba::NodeRef<Control> find_control(const guayaba::NodeRef<guayaba::Node>& root,
                                       const std::string& path) {
    return root->get_node_or_null<Control>(path);
}

void check_layout(Context& ctx, const guayaba::NodeRef<guayaba::Node>& root,
                  Vec2 screen) {
    const auto form = find_control(root, "screen/layout/body/form");
    const auto right = find_control(root, "screen/layout/body/right");
    const auto metrics = find_control(root, "screen/layout/body/right/metrics");
    const auto history = find_control(root, "screen/layout/body/right/history");
    const auto compare = find_control(root, "screen/layout/body/right/compare");
    const auto header = find_control(root, "screen/layout/header");
    const auto status = find_control(root, "screen/layout/status");

    CHECK(ctx, form.get() != nullptr);
    CHECK(ctx, right.get() != nullptr);
    CHECK(ctx, metrics.get() != nullptr);
    CHECK(ctx, history.get() != nullptr);
    CHECK(ctx, compare.get() != nullptr);
    if (!form || !right || !metrics || !history || !compare || !header || !status) {
        return;
    }

    CHECK(ctx, inside(form.get(), screen));
    CHECK(ctx, inside(right.get(), screen));
    CHECK(ctx, inside(metrics.get(), screen));
    CHECK(ctx, inside(history.get(), screen));
    CHECK(ctx, inside(compare.get(), screen));
    CHECK(ctx, inside(header.get(), screen));
    CHECK(ctx, inside(status.get(), screen));

    CHECK(ctx, form->get_rect().pos.x + form->get_rect().size.x <=
                   right->get_rect().pos.x);
    CHECK(ctx, metrics->get_rect().pos.y + metrics->get_rect().size.y <=
                   history->get_rect().pos.y);
    CHECK(ctx, history->get_rect().pos.y + history->get_rect().size.y <=
                   compare->get_rect().pos.y);
    CHECK(ctx, header->get_rect().pos.y + header->get_rect().size.y <=
                   form->get_rect().pos.y);
    CHECK(ctx, form->get_rect().pos.y + form->get_rect().size.y <=
                   status->get_rect().pos.y);

    const auto start = root->get_node_or_null<Button>(
        "screen/layout/body/form/form_column/form_actions/start");
    const auto stop = root->get_node_or_null<Button>(
        "screen/layout/body/form/form_column/form_actions/stop");
    const auto repeat = root->get_node_or_null<Button>(
        "screen/layout/body/form/form_column/form_actions/repeat");
    const auto export_button = root->get_node_or_null<Button>(
        "screen/layout/body/form/form_column/form_actions/export");

    CHECK(ctx, inside(start.get(), screen));
    CHECK(ctx, inside(stop.get(), screen));
    CHECK(ctx, inside(repeat.get(), screen));
    CHECK(ctx, inside(export_button.get(), screen));
}

void test_layout_80x24(Context& ctx) {
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
        check_layout(ctx, root, Vec2{80, 24});
        const auto title = root->get_node_or_null<Label>("screen/layout/header/title");
        CHECK(ctx, title.get() != nullptr);
        if (title.get() != nullptr) {
            CHECK_EQ(ctx, title->get_text(), std::string("mem-test"));
        }
        const auto history_list =
            root->get_node_or_null<ListView>(
                "screen/layout/body/right/history/history_box/history_list");
        CHECK(ctx, history_list.get() != nullptr);
        if (history_list.get() != nullptr) {
            CHECK_EQ(ctx, history_list->item_count(), std::size_t{0});
        }
    }

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

void test_layout_120x40(Context& ctx) {
    const SystemInfo info = test_system();
    FakeExperimentController controller(info);
    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{120, 40}, &terminal);
    manager.run_frame(renderer, {});
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() != nullptr) {
        check_layout(ctx, root, Vec2{120, 40});
    }

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

bool wait_done(FakeExperimentController& controller, SceneManager& manager,
               ConsoleRenderer& renderer, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool saw_done = false;
    while (std::chrono::steady_clock::now() < deadline) {
        manager.run_frame(renderer, {});
        if (controller.live().state == RunState::Done) {
            saw_done = true;
            break;
        }
        std::this_thread::sleep_for(5ms);
    }
    if (saw_done) {
        // Un frame extra: drena el resultado pendiente y actualiza los paneles.
        manager.run_frame(renderer, {});
    }
    return saw_done;
}

bool wait_done(ExperimentController& controller, SceneManager& manager,
               ConsoleRenderer& renderer, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool saw_done = false;
    while (std::chrono::steady_clock::now() < deadline) {
        manager.run_frame(renderer, {});
        if (controller.live().state == RunState::Done) {
            saw_done = true;
            break;
        }
        std::this_thread::sleep_for(5ms);
    }
    if (saw_done) {
        manager.run_frame(renderer, {});
    }
    return saw_done;
}

void test_start_stop_updates_panels(Context& ctx) {
    const SystemInfo info = test_system();
    FakeExperimentController controller(info);
    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{80, 24}, &terminal);
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() == nullptr) {
        return;
    }

    const auto start = root->get_node_or_null<Button>(
        "screen/layout/body/form/form_column/form_actions/start");
    CHECK(ctx, start.get() != nullptr && !start->is_disabled());
    CHECK_EQ(ctx, controller.live().state, RunState::Idle);

    manager.run_frame(renderer, {key_press(Key::F5)});
    CHECK(ctx, controller.live().state != RunState::Idle);
    CHECK(ctx, controller.live().state != RunState::Done);

    std::this_thread::sleep_for(30ms);
    manager.run_frame(renderer, {key_press(Key::F6)});
    CHECK(ctx, wait_done(controller, manager, renderer, 2000ms));

    const auto history_list = root->get_node_or_null<ListView>(
        "screen/layout/body/right/history/history_box/history_list");
    CHECK(ctx, history_list.get() != nullptr);
    if (history_list.get() != nullptr) {
        CHECK_EQ(ctx, history_list->item_count(), std::size_t{1});
        CHECK_EQ(ctx, history_list->selected(), 0);
    }

    const auto graph = root->get_node_or_null<Graph>(
        "screen/layout/body/right/metrics/metrics_box/metrics_graph");
    CHECK(ctx, graph.get() != nullptr);
    if (graph.get() != nullptr) {
        CHECK(ctx, !graph->get_values().empty());
    }

    const auto repeat = root->get_node_or_null<Button>(
        "screen/layout/body/form/form_column/form_actions/repeat");
    CHECK(ctx, repeat.get() != nullptr && !repeat->is_disabled());

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

void test_repeat_and_export(Context& ctx) {
    const SystemInfo info = test_system();
    const std::filesystem::path export_dir = temp_dir("ui-export");
    FakeExperimentController controller(info);

    ExperimentConfig config;
    config.threads = 2;
    config.work_units = 1000;
    config.wall_timeout_ms = 200;

    std::string error;
    CHECK(ctx, controller.start(config, &error));
    CHECK(ctx, error.empty());
    const auto deadline = std::chrono::steady_clock::now() + 2000ms;
    while (controller.live().state != RunState::Done &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    CHECK_EQ(ctx, controller.live().state, RunState::Done);

    auto scene =
        ui::build_main_scene(controller, info, guardrails_from(info), export_dir);
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{100, 30}, &terminal);
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() == nullptr) {
        return;
    }

    const auto history_list = root->get_node_or_null<ListView>(
        "screen/layout/body/right/history/history_box/history_list");
    CHECK(ctx, history_list.get() != nullptr);
    if (history_list.get() != nullptr) {
        CHECK_EQ(ctx, history_list->item_count(), std::size_t{1});
    }

    manager.run_frame(renderer, {key_press(Key::F8)});
    CHECK(ctx, std::filesystem::is_directory(export_dir));
    std::size_t summaries = 0;
    std::size_t details = 0;
    for (const auto& entry : std::filesystem::directory_iterator(export_dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("resumen_", 0) == 0) {
            ++summaries;
        } else if (name.rfind("detalle_", 0) == 0) {
            ++details;
        }
    }
    CHECK_EQ(ctx, summaries, std::size_t{1});
    CHECK_EQ(ctx, details, std::size_t{1});

    const auto status = root->get_node_or_null<Label>("screen/layout/status");
    CHECK(ctx, status.get() != nullptr);
    if (status.get() != nullptr) {
        CHECK(ctx, status->get_text().find("CSV exportado") != std::string::npos);
    }

    manager.run_frame(renderer, {key_press(Key::F7)});
    CHECK(ctx, controller.live().state != RunState::Idle);
    std::this_thread::sleep_for(30ms);
    manager.run_frame(renderer, {key_press(Key::F6)});
    CHECK(ctx, wait_done(controller, manager, renderer, 2000ms));

    const auto history_after = root->get_node_or_null<ListView>(
        "screen/layout/body/right/history/history_box/history_list");
    CHECK(ctx, history_after.get() != nullptr);
    if (history_after.get() != nullptr) {
        CHECK_EQ(ctx, history_after->item_count(), std::size_t{2});
    }

    std::error_code remove_error;
    std::filesystem::remove_all(export_dir, remove_error);

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

void test_real_controller_smoke(Context& ctx) {
    const SystemInfo info = test_system();
    ExperimentController controller(
        make_metrics_source(),
        std::make_unique<PosixProcessSupervisor>(worker_path()), info);

    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{100, 30}, &terminal);
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() == nullptr) {
        return;
    }

    manager.run_frame(renderer, {key_press(Key::F5)});
    CHECK(ctx, controller.live().state != RunState::Idle);
    CHECK(ctx, wait_done(controller, manager, renderer, 5000ms));

    const auto history_list = root->get_node_or_null<ListView>(
        "screen/layout/body/right/history/history_box/history_list");
    CHECK(ctx, history_list.get() != nullptr);
    if (history_list.get() != nullptr) {
        CHECK_EQ(ctx, history_list->item_count(), std::size_t{1});
    }

    const auto graph = root->get_node_or_null<Graph>(
        "screen/layout/body/right/metrics/metrics_box/metrics_graph");
    CHECK(ctx, graph.get() != nullptr);
    if (graph.get() != nullptr) {
        CHECK(ctx, !graph->get_values().empty());
    }

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

}  // namespace

Suite main_scene_suite() {
    return {
        {"layout_80x24", test_layout_80x24},
        {"layout_120x40", test_layout_120x40},
        {"start_stop_updates_panels", test_start_stop_updates_panels},
        {"repeat_and_export", test_repeat_and_export},
        {"real_controller_smoke", test_real_controller_smoke},
    };
}

}  // namespace memtest::test

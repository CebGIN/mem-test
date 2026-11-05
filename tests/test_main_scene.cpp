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
#include "ui/UiFormat.hpp"

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
    const auto main_page =
        find_control(root, "screen/layout/body/main_page");
    const auto form = find_control(root, "screen/layout/body/main_page/form");
    const auto right = find_control(root, "screen/layout/body/main_page/right");
    const auto metrics = find_control(root, "screen/layout/body/main_page/right/metrics");
    const auto history = find_control(root, "screen/layout/body/main_page/right/history");
    const auto compare = find_control(root, "screen/layout/body/compare_page");
    const auto header = find_control(root, "screen/layout/header");
    const auto status = find_control(root, "screen/layout/status");

    CHECK(ctx, main_page.get() != nullptr);
    CHECK(ctx, form.get() != nullptr);
    CHECK(ctx, right.get() != nullptr);
    CHECK(ctx, metrics.get() != nullptr);
    CHECK(ctx, history.get() != nullptr);
    CHECK(ctx, compare.get() != nullptr);
    if (!main_page || !form || !right || !metrics || !history || !compare ||
        !header || !status) {
        return;
    }

    // La vista de comparación existe pero arranca oculta.
    CHECK(ctx, main_page->is_visible());
    CHECK(ctx, !compare->is_visible());

    CHECK(ctx, inside(main_page.get(), screen));
    CHECK(ctx, inside(form.get(), screen));
    CHECK(ctx, inside(right.get(), screen));
    CHECK(ctx, inside(metrics.get(), screen));
    CHECK(ctx, inside(history.get(), screen));
    CHECK(ctx, inside(header.get(), screen));
    CHECK(ctx, inside(status.get(), screen));

    CHECK(ctx, form->get_rect().pos.x + form->get_rect().size.x <=
                   right->get_rect().pos.x);
    CHECK(ctx, metrics->get_rect().pos.y + metrics->get_rect().size.y <=
                   history->get_rect().pos.y);
    CHECK(ctx, header->get_rect().pos.y + header->get_rect().size.y <=
                   form->get_rect().pos.y);
    CHECK(ctx, form->get_rect().pos.y + form->get_rect().size.y <=
                   status->get_rect().pos.y);

    const auto start = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/form/form_column/form_actions/form_actions_row1/start");
    const auto stop = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/form/form_column/form_actions/form_actions_row1/stop");
    const auto repeat = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/form/form_column/form_actions/form_actions_row2/repeat");
    const auto export_button = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/form/form_column/form_actions/form_actions_row2/export");

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
        // El monitor del sistema debe estar visible incluso en Idle.
        const auto system =
            root->get_node_or_null<Label>("screen/layout/header/system");
        CHECK(ctx, system.get() != nullptr);
        if (system.get() != nullptr) {
            CHECK(ctx, system->get_text().find("CPU") != std::string::npos);
        }
        const auto history_list =
            root->get_node_or_null<ListView>(
                "screen/layout/body/main_page/right/history/history_box/history_list");
        CHECK(ctx, history_list.get() != nullptr);
        if (history_list.get() != nullptr) {
            CHECK_EQ(ctx, history_list->item_count(), std::size_t{0});
        }
        const auto compare_button = root->get_node_or_null<Button>(
            "screen/layout/body/main_page/right/history/history_box/"
            "history_actions/history_compare");
        CHECK(ctx, compare_button.get() != nullptr);
        if (compare_button.get() != nullptr) {
            CHECK(ctx, compare_button->is_disabled());
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
        // Dos frames extra: drena el resultado y deja que el layout aplique la
        // visibilidad de la lista y la gráfica.
        manager.run_frame(renderer, {});
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
        "screen/layout/body/main_page/form/form_column/form_actions/form_actions_row1/start");
    CHECK(ctx, start.get() != nullptr && !start->is_disabled());
    CHECK_EQ(ctx, controller.live().state, RunState::Idle);

    manager.run_frame(renderer, {key_press(Key::F5)});
    CHECK(ctx, controller.live().state != RunState::Idle);
    CHECK(ctx, controller.live().state != RunState::Done);

    std::this_thread::sleep_for(30ms);
    manager.run_frame(renderer, {key_press(Key::F6)});
    CHECK(ctx, wait_done(controller, manager, renderer, 2000ms));

    const auto history_list = root->get_node_or_null<ListView>(
        "screen/layout/body/main_page/right/history/history_box/history_list");
    CHECK(ctx, history_list.get() != nullptr);
    if (history_list.get() != nullptr) {
        CHECK_EQ(ctx, history_list->item_count(), std::size_t{1});
        CHECK_EQ(ctx, history_list->selected(), 0);
        CHECK(ctx, history_list->get_rect().size.y > 0);
    }

    const auto graph = root->get_node_or_null<Graph>(
        "screen/layout/body/main_page/right/metrics/metrics_box/metrics_graph");
    CHECK(ctx, graph.get() != nullptr);
    if (graph.get() != nullptr) {
        CHECK(ctx, !graph->get_values().empty());
        CHECK(ctx, graph->get_rect().size.y > 0);
    }

    const auto repeat = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/form/form_column/form_actions/form_actions_row2/repeat");
    CHECK(ctx, repeat.get() != nullptr && !repeat->is_disabled());

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

void test_workload_tabs(Context& ctx) {
    const SystemInfo info = test_system();
    FakeExperimentController controller(info);
    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{120, 40}, &terminal);
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() == nullptr) {
        return;
    }

    const auto tab = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/form/form_column/form_scroll_margin/form_scroll/"
        "form_box/workload_tabs/workload_tab_2");
    CHECK(ctx, tab.get() != nullptr);
    if (tab.get() != nullptr) {
        tab->emit_signal("clicked");
    }
    manager.run_frame(renderer, {});

    const auto workload = root->get_node_or_null<ListView>(
        "screen/layout/body/main_page/form/form_column/form_scroll_margin/form_scroll/"
        "form_box/workload");
    CHECK(ctx, workload.get() != nullptr);
    if (workload.get() != nullptr) {
        CHECK_EQ(ctx, workload->selected(), 2);
    }
    const std::string form =
        "screen/layout/body/main_page/form/form_column/form_scroll_margin/form_scroll/"
        "form_box/";
    auto control = [&](const std::string& name) {
        return root->get_node_or_null<Control>(form + name);
    };
    auto click_tab = [&](int index) {
        const auto tab = root->get_node_or_null<Button>(
            form + "workload_tabs/workload_tab_" + std::to_string(index));
        if (tab.get() != nullptr) {
            tab->emit_signal("clicked");
        }
        manager.run_frame(renderer, {});
    };
    auto visible = [&](const std::string& name) {
        const auto node = control(name);
        return node.get() != nullptr && node->is_visible();
    };

    const auto memory_row = control("memory_row");
    CHECK(ctx, memory_row.get() != nullptr);
    if (memory_row.get() != nullptr) {
        CHECK(ctx, memory_row->is_visible());
    }
    CHECK(ctx, visible("force_swap"));
    CHECK(ctx, !visible("cpu_limit"));
    CHECK(ctx, !visible("stride_row"));
    CHECK(ctx, !visible("threads_row"));

    click_tab(1);  // Limite de CPU
    CHECK(ctx, visible("cpu_limit_mode_row"));
    CHECK(ctx, visible("cpu_seconds_row"));
    CHECK(ctx, !visible("cpu_quota_row"));
    CHECK(ctx, !visible("cpu_limit"));
    CHECK(ctx, !visible("threads_row"));
    CHECK(ctx, !visible("worker_row"));
    CHECK(ctx, !visible("work_units_row"));

    click_tab(3);  // Acceso compartido
    CHECK(ctx, visible("threads_row"));
    CHECK(ctx, visible("worker_row"));
    CHECK(ctx, visible("sync_row"));
    CHECK(ctx, !visible("work_units_row"));

    click_tab(0);  // Concurrencia
    CHECK(ctx, visible("work_units_row"));
    CHECK(ctx, visible("threads_row"));
    CHECK(ctx, visible("worker_row"));
    CHECK(ctx, visible("cpu_limit"));
    CHECK(ctx, !visible("sync_row"));

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

bool seed_run(FakeExperimentController& controller, int threads) {
    ExperimentConfig config;
    config.threads = threads;
    config.work_units = 1000;
    config.wall_timeout_ms = 500;

    std::string error;
    if (!controller.start(config, &error)) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + 3000ms;
    while (controller.live().state != RunState::Done &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    return controller.live().state == RunState::Done;
}

void test_compare_view(Context& ctx) {
    const SystemInfo info = test_system();
    FakeExperimentController controller(info);
    // A queda en la última corrida (2 hilos); el selector ofrece las otras dos
    // (8 y 4 hilos), así los deltas de B son positivos y detectables.
    CHECK(ctx, seed_run(controller, 8));
    CHECK(ctx, seed_run(controller, 4));
    CHECK(ctx, seed_run(controller, 2));

    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{120, 40}, &terminal);
    manager.run_frame(renderer, {});
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() == nullptr) {
        return;
    }

    const auto history_list = root->get_node_or_null<ListView>(
        "screen/layout/body/main_page/right/history/history_box/history_list");
    CHECK(ctx, history_list.get() != nullptr);
    if (history_list.get() != nullptr) {
        CHECK_EQ(ctx, history_list->item_count(), std::size_t{3});
        CHECK_EQ(ctx, history_list->selected(), 2);
    }

    const auto main_page =
        find_control(root, "screen/layout/body/main_page");
    const auto compare_page =
        find_control(root, "screen/layout/body/compare_page");
    CHECK(ctx, main_page.get() != nullptr);
    CHECK(ctx, compare_page.get() != nullptr);
    CHECK(ctx, main_page->is_visible());
    CHECK(ctx, !compare_page->is_visible());

    const auto compare_button = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/right/history/history_box/"
        "history_actions/history_compare");
    CHECK(ctx, compare_button.get() != nullptr);
    if (compare_button.get() != nullptr) {
        CHECK(ctx, !compare_button->is_disabled());
        compare_button->emit_signal("clicked");
    }
    manager.run_frame(renderer, {});

    CHECK(ctx, !main_page->is_visible());
    CHECK(ctx, compare_page->is_visible());

    const auto primary_details = root->get_node_or_null<Label>(
        "screen/layout/body/compare_page/compare_left/compare_left_box/"
        "compare_primary_details");
    const auto primary_title = root->get_node_or_null<Label>(
        "screen/layout/body/compare_page/compare_left/compare_left_box/"
        "compare_left_header/compare_left_title");
    const auto secondary_details = root->get_node_or_null<Label>(
        "screen/layout/body/compare_page/compare_right/compare_right_box/"
        "compare_secondary_details");
    const auto secondary_title = root->get_node_or_null<Label>(
        "screen/layout/body/compare_page/compare_right/compare_right_box/"
        "compare_right_header/compare_secondary_title");
    const auto selector = root->get_node_or_null<ListView>(
        "screen/layout/body/compare_page/compare_right/compare_right_box/"
        "compare_selector");
    CHECK(ctx, primary_details.get() != nullptr);
    CHECK(ctx, primary_title.get() != nullptr);
    CHECK(ctx, secondary_details.get() != nullptr);
    CHECK(ctx, secondary_title.get() != nullptr);
    CHECK(ctx, selector.get() != nullptr);
    if (primary_details.get() != nullptr &&
        primary_title.get() != nullptr &&
        secondary_details.get() != nullptr &&
        secondary_title.get() != nullptr &&
        selector.get() != nullptr) {
        // A es la corrida seleccionada en el historial; B la más reciente de
        // las otras dos. El run_id vive en el título de cada ficha.
        CHECK(ctx, primary_title->get_text().find("fake-3") != std::string::npos);
        CHECK_EQ(ctx, selector->item_count(), std::size_t{2});
        CHECK_EQ(ctx, selector->selected(), 1);
        // El selector mide lo que mide su contenido: sin hueco antes de B.
        CHECK_EQ(ctx, selector->get_custom_minimum_size().y, 2);
        CHECK(ctx, secondary_title->get_text().find("fake-2") !=
                       std::string::npos);
        // La ficha cubre lo que pide el requerimiento y el delta respecto de A.
        CHECK(ctx, secondary_details->get_text().find("RSS pico") !=
                       std::string::npos);
        CHECK(ctx, secondary_details->get_text().find("Virtual") !=
                       std::string::npos);
        CHECK(ctx, secondary_details->get_text().find("SWAP carga") !=
                       std::string::npos);
        CHECK(ctx, secondary_details->get_text().find("Fallos") !=
                       std::string::npos);
        CHECK(ctx, secondary_details->get_text().find("Lím. mem") !=
                       std::string::npos);
        CHECK(ctx, secondary_details->get_text().find("(+") != std::string::npos);
    }

    // El selector tiene el foco: ↑ mueve B a la corrida anterior.
    manager.run_frame(renderer, {key_press(Key::UP)});
    if (selector.get() != nullptr) {
        CHECK_EQ(ctx, selector->selected(), 0);
    }
    if (secondary_title.get() != nullptr) {
        CHECK(ctx, secondary_title->get_text().find("fake-1") !=
                       std::string::npos);
    }

    // Esc vuelve a la pantalla principal y reselecciona A.
    manager.run_frame(renderer, {key_press(Key::ESCAPE)});
    CHECK(ctx, main_page->is_visible());
    CHECK(ctx, !compare_page->is_visible());
    if (history_list.get() != nullptr) {
        CHECK_EQ(ctx, history_list->selected(), 2);
    }

    // Enter/doble clic sobre otra fila abre la comparación con esa corrida.
    if (history_list.get() != nullptr) {
        history_list->set_selected(0);
        manager.run_frame(renderer, {});
        history_list->emit_signal("item_activated", 0);
        manager.run_frame(renderer, {});
        CHECK(ctx, compare_page->is_visible());
        if (primary_title.get() != nullptr) {
            CHECK(ctx, primary_title->get_text().find("fake-1") !=
                           std::string::npos);
        }
    }

    // Repetir A vuelve a la pantalla principal y lanza la corrida copiada.
    const auto repeat_button = root->get_node_or_null<Button>(
        "screen/layout/body/compare_page/compare_left/compare_left_box/"
        "compare_actions/compare_repeat");
    CHECK(ctx, repeat_button.get() != nullptr);
    if (repeat_button.get() != nullptr) {
        CHECK(ctx, !repeat_button->is_disabled());
        repeat_button->emit_signal("clicked");
        manager.run_frame(renderer, {});
        CHECK(ctx, main_page->is_visible());
        CHECK(ctx, !compare_page->is_visible());
        CHECK(ctx, controller.live().state != RunState::Idle);
        controller.stop();
    }

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

void test_detail_block(Context& ctx) {
    ExperimentResult run;
    run.run_id = "run-1";
    run.config.workload = WorkloadKind::SharedCounter;
    run.config.threads = 4;
    run.config.sync_mode = SyncMode::None;
    run.outcome = RunOutcome::Completed;
    run.duration = 5000ms;
    run.peak.vm_size = 40 * kMiB;
    run.peak.vm_rss = 4 * kMiB;
    run.peak.sys_swap_total = 4 * 1024 * kMiB;
    run.peak.minor_faults = 123;
    run.peak.major_faults = 2;
    run.expected_value = 1000;
    run.observed_value = 950;
    run.limits.memory_kind = MemoryLimitKind::AddressSpace;
    run.limits.memory_status = LimitStatus::NotApplied;
    run.system_cpu_peak_percent = 60.0;
    run.system_ram_delta = 2 * kMiB;
    run.system_baseline.swap_total = 4 * 1024 * kMiB;
    run.system_peak.swap_used = 1 * 1024 * kMiB;

    const std::string block = ui::detail_block(run);
    CHECK(ctx, block.find("run-1 · Acceso compartido") != std::string::npos);
    CHECK_EQ(ctx, ui::detail_title(run),
             std::string("run-1 · Acceso compartido"));
    CHECK(ctx, ui::detail_fields(run).find("run-1") == std::string::npos);
    CHECK_EQ(ctx, block,
             ui::detail_title(run) + "\n" + ui::detail_fields(run));
    CHECK(ctx, block.find("Hilos: 4 · sin sync · 5.00 s") != std::string::npos);
    CHECK(ctx, block.find("Resultado: Completada") != std::string::npos);
    CHECK(ctx, block.find("RSS pico: 4.0 MiB") != std::string::npos);
    CHECK(ctx, block.find("SWAP carga: 0.0 MiB") != std::string::npos);
    CHECK(ctx, block.find("Fallos: 123 men / 2 may") != std::string::npos);
    CHECK(ctx, block.find("Sist.: CPU 60% · ΔRAM 2.0 MiB") !=
                   std::string::npos);
    CHECK(ctx, block.find("SWAP sist.: 1.0 GiB/4.0 GiB") !=
                   std::string::npos);
    CHECK(ctx, block.find("Lím. mem: AS no aplicado") != std::string::npos);
    CHECK(ctx, block.find("Lím. cpu: sin límite") != std::string::npos);
    CHECK(ctx, block.find("Perdidas: 50") != std::string::npos);

    // La ficha de B agrega el delta respecto de A.
    ExperimentResult other = run;
    other.run_id = "run-2";
    other.peak.vm_rss = 6 * kMiB;
    other.peak.vm_size = 44 * kMiB;
    const std::string compared = ui::detail_block(other, &run);
    CHECK(ctx, compared.find("RSS pico: 6.0 MiB (+2.0 MiB)") !=
                   std::string::npos);
    CHECK(ctx, compared.find("Virtual: 44.0 MiB (+4.0 MiB)") !=
                   std::string::npos);
}

void test_compare_single_run(Context& ctx) {
    const SystemInfo info = test_system();
    FakeExperimentController controller(info);
    CHECK(ctx, seed_run(controller, 2));

    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer renderer(Vec2{100, 30}, &terminal);
    manager.run_frame(renderer, {});
    manager.run_frame(renderer, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() == nullptr) {
        return;
    }

    const auto compare_button = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/right/history/history_box/"
        "history_actions/history_compare");
    CHECK(ctx, compare_button.get() != nullptr);
    if (compare_button.get() != nullptr) {
        compare_button->emit_signal("clicked");
    }
    manager.run_frame(renderer, {});

    const auto selector = root->get_node_or_null<ListView>(
        "screen/layout/body/compare_page/compare_right/compare_right_box/"
        "compare_selector");
    const auto secondary_details = root->get_node_or_null<Label>(
        "screen/layout/body/compare_page/compare_right/compare_right_box/"
        "compare_secondary_details");
    const auto secondary_title = root->get_node_or_null<Label>(
        "screen/layout/body/compare_page/compare_right/compare_right_box/"
        "compare_right_header/compare_secondary_title");
    CHECK(ctx, selector.get() != nullptr);
    CHECK(ctx, secondary_details.get() != nullptr);
    CHECK(ctx, secondary_title.get() != nullptr);
    if (selector.get() != nullptr) {
        CHECK_EQ(ctx, selector->item_count(), std::size_t{0});
        CHECK(ctx, !selector->is_visible());
    }
    if (secondary_title.get() != nullptr) {
        CHECK(ctx, secondary_title->get_text().find("Sin otra ejecución") !=
                       std::string::npos);
    }
    if (secondary_details.get() != nullptr) {
        CHECK(ctx, secondary_details->get_text().find("Agregá otra corrida") !=
                       std::string::npos);
    }

    std::unique_ptr<guayaba::Node> owned = manager.take_scene();
    owned.reset();
}

void test_compare_layout(Context& ctx) {
    const SystemInfo info = test_system();
    FakeExperimentController controller(info);
    CHECK(ctx, seed_run(controller, 2));
    CHECK(ctx, seed_run(controller, 4));

    auto scene = ui::build_main_scene(controller, info, guardrails_from(info));
    SceneManager& manager = SceneManager::get_instance();
    manager.change_scene(std::move(scene));

    CapturingTerminal terminal;
    ConsoleRenderer small(Vec2{80, 24}, &terminal);
    manager.run_frame(small, {});
    manager.run_frame(small, {});

    guayaba::NodeRef<guayaba::Node> root = manager.get_current_scene();
    CHECK(ctx, root.get() != nullptr);
    if (root.get() == nullptr) {
        return;
    }

    const auto compare_button = root->get_node_or_null<Button>(
        "screen/layout/body/main_page/right/history/history_box/"
        "history_actions/history_compare");
    CHECK(ctx, compare_button.get() != nullptr);
    if (compare_button.get() != nullptr) {
        compare_button->emit_signal("clicked");
    }
    manager.run_frame(small, {});

    const auto compare_page =
        find_control(root, "screen/layout/body/compare_page");
    const auto left = find_control(root, "screen/layout/body/compare_page/compare_left");
    const auto right = find_control(root, "screen/layout/body/compare_page/compare_right");
    const auto selector = find_control(
        root,
        "screen/layout/body/compare_page/compare_right/compare_right_box/"
        "compare_selector");
    const auto back = find_control(
        root,
        "screen/layout/body/compare_page/compare_left/compare_left_box/"
        "compare_actions/compare_back");
    const auto repeat = find_control(
        root,
        "screen/layout/body/compare_page/compare_left/compare_left_box/"
        "compare_actions/compare_repeat");
    const auto status = find_control(root, "screen/layout/status");

    CHECK(ctx, compare_page.get() != nullptr);
    CHECK(ctx, left.get() != nullptr);
    CHECK(ctx, right.get() != nullptr);
    CHECK(ctx, selector.get() != nullptr);
    CHECK(ctx, back.get() != nullptr);
    CHECK(ctx, repeat.get() != nullptr);
    CHECK(ctx, status.get() != nullptr);
    if (!compare_page || !left || !right || !selector || !back || !repeat ||
        !status) {
        return;
    }

    const auto check_at = [&](Vec2 screen, ConsoleRenderer& renderer) {
        manager.run_frame(renderer, {});
        manager.run_frame(renderer, {});
        CHECK(ctx, inside(compare_page.get(), screen));
        CHECK(ctx, inside(left.get(), screen));
        CHECK(ctx, inside(right.get(), screen));
        CHECK(ctx, inside(selector.get(), screen));
        CHECK(ctx, inside(back.get(), screen));
        CHECK(ctx, inside(repeat.get(), screen));
        CHECK(ctx, left->get_rect().pos.x + left->get_rect().size.x <=
                       right->get_rect().pos.x);
        CHECK(ctx, right->get_rect().pos.y + right->get_rect().size.y <=
                       status->get_rect().pos.y);
    };

    check_at(Vec2{80, 24}, small);
    ConsoleRenderer large(Vec2{120, 40}, &terminal);
    check_at(Vec2{120, 40}, large);

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
        "screen/layout/body/main_page/right/history/history_box/history_list");
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
        "screen/layout/body/main_page/right/history/history_box/history_list");
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
    CHECK(ctx, wait_done(controller, manager, renderer, 15000ms));

    const auto history_list = root->get_node_or_null<ListView>(
        "screen/layout/body/main_page/right/history/history_box/history_list");
    CHECK(ctx, history_list.get() != nullptr);
    if (history_list.get() != nullptr) {
        CHECK_EQ(ctx, history_list->item_count(), std::size_t{1});
    }

    const auto graph = root->get_node_or_null<Graph>(
        "screen/layout/body/main_page/right/metrics/metrics_box/metrics_graph");
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
        {"workload_tabs", test_workload_tabs},
        {"detail_block", test_detail_block},
        {"compare_view", test_compare_view},
        {"compare_single_run", test_compare_single_run},
        {"compare_layout", test_compare_layout},
        {"repeat_and_export", test_repeat_and_export},
        {"real_controller_smoke", test_real_controller_smoke},
    };
}

}  // namespace memtest::test

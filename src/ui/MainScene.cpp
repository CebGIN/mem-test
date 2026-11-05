#include "ui/MainScene.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "ui/CompareView.hpp"
#include "ui/ExperimentForm.hpp"
#include "ui/HistoryPanel.hpp"
#include "ui/MetricsPanel.hpp"
#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

using guayaba::Control;
using guayaba::FocusManager;
using guayaba::HBox;
using guayaba::InputEvent;
using guayaba::Key;
using guayaba::Label;
using guayaba::NodeRef;
using guayaba::PanelContainer;
using guayaba::SceneManager;
using guayaba::Script;
using guayaba::SplitContainer;
using guayaba::SplitOrientation;
using guayaba::VBox;

const char* kDefaultStatus =
    "Listo. F5 inicia · F6 detiene · F7 repite · F8 exporta · F9 compara · Esc sale";

class MainSceneScript : public Script<guayaba::Node> {
public:
    MainSceneScript(IExperimentController* controller, SystemInfo info,
                    std::filesystem::path export_dir, ExperimentForm form,
                    MetricsPanel metrics, HistoryPanel history_panel,
                    CompareView compare, NodeRef<Control> main_page,
                    NodeRef<Label> state, NodeRef<Label> system,
                    NodeRef<Label> status)
        : controller_(controller),
          info_(std::move(info)),
          export_dir_(std::move(export_dir)),
          form_(std::move(form)),
          metrics_(std::move(metrics)),
          history_panel_(std::move(history_panel)),
          compare_(std::move(compare)),
          main_page_(main_page),
          state_label_(state),
          system_label_(system),
          status_label_(status) {}

    void on_attached() override {
        form_.start_button()->connect_script_signal(
            "clicked", this, &MainSceneScript::start_run);
        form_.stop_button()->connect_script_signal(
            "clicked", this, &MainSceneScript::stop_run);
        form_.repeat_button()->connect_script_signal(
            "clicked", this, &MainSceneScript::repeat_selected);
        form_.export_button()->connect_script_signal(
            "clicked", this, &MainSceneScript::export_history);
        form_.workload_button(0)->connect_script_signal(
            "clicked", this, &MainSceneScript::select_concurrency);
        form_.workload_button(1)->connect_script_signal(
            "clicked", this, &MainSceneScript::select_cpu_limit);
        form_.workload_button(2)->connect_script_signal(
            "clicked", this, &MainSceneScript::select_memory_pressure);
        form_.workload_button(3)->connect_script_signal(
            "clicked", this, &MainSceneScript::select_shared_counter);

        history_panel_.compare_button()->connect_script_signal(
            "clicked", this, &MainSceneScript::compare_selected);
        history_panel_.list()->connect_script_signal(
            "item_activated", this, &MainSceneScript::open_compare_at);
        compare_.back_button()->connect_script_signal(
            "clicked", this, &MainSceneScript::close_compare);
        compare_.repeat_button()->connect_script_signal(
            "clicked", this, &MainSceneScript::repeat_primary);
        compare_.list()->connect_script_signal(
            "selection_changed", this, &MainSceneScript::secondary_changed);
    }

    void on_enter_tree() override {
        if (controller_ == nullptr) {
            return;
        }
        try {
            history_ = controller_->history();
            refresh_panels();
            if (!history_.empty()) {
                history_panel_.select_last();
            }
            drain_results();
            refresh_ui();
        } catch (const std::exception& exception) {
            set_notice(std::string("Error al cargar el historial: ") + exception.what(),
                       true);
        } catch (...) {
            set_notice("Error desconocido al cargar el historial.", true);
        }
    }

    void on_process(double) override {
        if (controller_ == nullptr) {
            return;
        }
        try {
            drain_results();
            refresh_ui();
        } catch (const std::exception& exception) {
            set_notice(std::string("Error de UI: ") + exception.what(), true);
        } catch (...) {
            set_notice("Error desconocido de UI.", true);
        }
    }

    void on_unhandled_input(const InputEvent& event) override {
        if (event.type != InputEvent::Type::KEY || !event.pressed) {
            return;
        }

        // En la vista de comparación solo Esc vuelve y F8 exporta. Lanzar o
        // repetir desde acá queda deshabilitado a propósito.
        if (compare_visible_) {
            switch (event.key) {
                case Key::ESCAPE:
                    close_compare();
                    break;
                case Key::F8:
                    export_history();
                    break;
                default:
                    break;
            }
            return;
        }

        switch (event.key) {
            case Key::F5:
                start_run();
                break;
            case Key::F6:
                stop_run();
                break;
            case Key::F7:
                repeat_selected();
                break;
            case Key::F8:
                export_history();
                break;
            case Key::F9:
                compare_selected();
                break;
            case Key::ESCAPE:
            case Key::Q:
                SceneManager::get_instance().stop_running();
                break;
            default:
                break;
        }
    }

private:
    void select_workload(WorkloadKind kind) {
        form_.set_workload(kind);
        clear_notice();
    }

    void select_concurrency() {
        select_workload(WorkloadKind::Concurrency);
    }

    void select_cpu_limit() {
        select_workload(WorkloadKind::CpuLimit);
    }

    void select_memory_pressure() {
        select_workload(WorkloadKind::MemoryPressure);
    }

    void select_shared_counter() {
        select_workload(WorkloadKind::SharedCounter);
    }

    void start_run() {
        if (controller_ == nullptr) {
            return;
        }
        try {
            draft_ = form_.read(draft_);
            const ValidationReport report = controller_->validate(draft_);
            if (!report.ok()) {
                set_notice("Configuración inválida: " + report.issues.front().message, true);
                return;
            }

            std::string error;
            if (!controller_->start(draft_, &error)) {
                set_notice(error.empty() ? "No se pudo iniciar la ejecución."
                                         : "No se pudo iniciar: " + error,
                           true);
                return;
            }
            clear_notice();
        } catch (const std::exception& exception) {
            set_notice(std::string("Error al iniciar: ") + exception.what(), true);
        } catch (...) {
            set_notice("Error desconocido al iniciar.", true);
        }
    }

    void stop_run() {
        if (controller_ == nullptr) {
            return;
        }
        try {
            clear_notice();
            controller_->stop();
        } catch (const std::exception& exception) {
            set_notice(std::string("Error al detener: ") + exception.what(), true);
        } catch (...) {
            set_notice("Error desconocido al detener.", true);
        }
    }

    void repeat_selected() {
        if (controller_ == nullptr) {
            return;
        }
        const int index = history_panel_.selected_index();
        if (index < 0 || static_cast<std::size_t>(index) >= history_.size()) {
            set_notice("Elegí una ejecución del historial para repetir.", true);
            return;
        }
        try {
            const ExperimentConfig config = history_[static_cast<std::size_t>(index)].config;
            form_.write(config);
            draft_ = config;
            start_run();
        } catch (const std::exception& exception) {
            set_notice(std::string("Error al repetir: ") + exception.what(), true);
        } catch (...) {
            set_notice("Error desconocido al repetir.", true);
        }
    }

    void compare_selected() {
        open_compare_at(history_panel_.selected_index());
    }

    void open_compare_at(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= history_.size()) {
            set_notice("Elegí una ejecución del historial para comparar.", true);
            return;
        }
        try {
            compare_primary_id_ = history_[static_cast<std::size_t>(index)].run_id;
            syncing_compare_ = true;
            compare_.set_results(history_, compare_primary_id_);
            syncing_compare_ = false;

            main_page_->set_visible(false);
            compare_.page()->set_visible(true);
            compare_visible_ = true;
            if (compare_.list()->is_visible()) {
                compare_.list()->grab_focus();
            }
            clear_notice();
            refresh_ui();
        } catch (const std::exception& exception) {
            syncing_compare_ = false;
            set_notice(std::string("Error al comparar: ") + exception.what(), true);
        } catch (...) {
            syncing_compare_ = false;
            set_notice("Error desconocido al comparar.", true);
        }
    }

    void close_compare() {
        if (!compare_visible_) {
            return;
        }
        compare_visible_ = false;
        compare_.page()->set_visible(false);
        main_page_->set_visible(true);
        FocusManager::get_instance().clear();

        const int index = index_of_run(compare_primary_id_);
        if (index >= 0) {
            history_panel_.select_index(index);
        }
        compare_primary_id_.clear();
        refresh_ui();
    }

    void secondary_changed(int index) {
        if (!compare_visible_ || syncing_compare_) {
            return;
        }
        compare_.select_secondary(index);
        refresh_ui();
    }

    void repeat_primary() {
        if (controller_ == nullptr) {
            return;
        }
        const ExperimentResult* run = compare_.primary();
        if (run == nullptr) {
            return;
        }
        const ExperimentConfig config = run->config;
        close_compare();
        try {
            form_.write(config);
            draft_ = config;
            start_run();
        } catch (const std::exception& exception) {
            set_notice(std::string("Error al repetir: ") + exception.what(), true);
        } catch (...) {
            set_notice("Error desconocido al repetir.", true);
        }
    }

    void export_history() {
        if (controller_ == nullptr) {
            return;
        }
        try {
            std::string error;
            if (controller_->export_csv(export_dir_, &error)) {
                set_notice("CSV exportado en " + export_dir_.string(), false);
            } else {
                set_notice(error.empty() ? "No se pudo exportar el CSV."
                                         : "No se pudo exportar: " + error,
                           true);
            }
        } catch (const std::exception& exception) {
            set_notice(std::string("Error al exportar: ") + exception.what(), true);
        } catch (...) {
            set_notice("Error desconocido al exportar.", true);
        }
    }

    void drain_results() {
        std::vector<ExperimentResult> results = controller_->take_results();
        if (results.empty()) {
            return;
        }
        for (ExperimentResult& result : results) {
            const bool already_known =
                std::any_of(history_.begin(), history_.end(),
                            [&result](const ExperimentResult& known) {
                                return known.run_id == result.run_id;
                            });
            if (!already_known) {
                history_.push_back(std::move(result));
            }
        }
        clear_notice();
        refresh_panels();
        if (history_panel_.selected_index() < 0 && !history_.empty()) {
            history_panel_.select_last();
        }
    }

    void refresh_panels() {
        history_panel_.set_results(history_);
        if (compare_visible_) {
            syncing_compare_ = true;
            compare_.set_results(history_, compare_primary_id_);
            syncing_compare_ = false;
        }
    }

    void refresh_ui() {
        const LiveState live = controller_->live();
        draft_ = form_.read(draft_);
        const ValidationReport report = controller_->validate(draft_);
        form_.refresh(draft_, report);
        metrics_.refresh(live);

        const bool running =
            live.state != RunState::Idle && live.state != RunState::Done;
        const bool has_selection = history_panel_.selected_index() >= 0;

        form_.start_button()->set_disabled(running || !report.ok());
        form_.stop_button()->set_disabled(!running);
        form_.repeat_button()->set_disabled(running || !has_selection);
        form_.export_button()->set_disabled(history_.empty());
        history_panel_.compare_button()->set_disabled(!has_selection);

        if (Label* label = state_label_.get()) {
            label->set_text("Estado: " + state_text(live.state));
        }
        if (Label* label = system_label_.get()) {
            label->set_text(system_summary_text(live));
        }

        std::string status;
        bool error = false;
        if (!notice_.empty()) {
            status = notice_;
            error = notice_is_error_;
        } else if (compare_visible_) {
            status = "Comparación " + compare_primary_id_;
            const std::string second = compare_.secondary_id();
            status += second.empty() ? " · sin segunda ejecución"
                                     : " vs " + second;
            status += " · Esc vuelve · ↑↓ elige";
        } else if (!live.message.empty()) {
            status = live.message;
            if (live.state == RunState::Done && !history_.empty()) {
                status += " · " + limits_summary(history_.back());
            }
        } else {
            status = kDefaultStatus;
        }

        if (Label* label = status_label_.get()) {
            label->set_text(" " + status);
            label->set_text_color(error ? "BRIGHT_RED" : "WHITE");
        }
    }

    std::string system_summary_text(const LiveState& live) const {
        if (live.system.mem_total == 0) {
            std::ostringstream out;
            out << "Sistema: " << std::max(1, info_.cores)
                << " núcleos · midiendo...";
            return out.str();
        }
        std::ostringstream out;
        out << "CPU " << std::fixed << std::setprecision(0)
            << live.system.cpu_total_percent << "% · RAM "
            << gib(live.system.mem_used) << "/" << gib(live.system.mem_total)
            << " · ";
        if (live.system.swap_available) {
            out << "SWAP " << gib(live.system.swap_used) << "/"
                << gib(live.system.swap_total);
        } else {
            out << "SWAP no disponible";
        }
        return out.str();
    }

    int index_of_run(const std::string& run_id) const {
        for (std::size_t index = 0; index < history_.size(); ++index) {
            if (history_[index].run_id == run_id) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    void set_notice(std::string message, bool error) {
        notice_ = std::move(message);
        notice_is_error_ = error;
    }

    void clear_notice() {
        notice_.clear();
        notice_is_error_ = false;
    }

    IExperimentController* controller_ = nullptr;
    SystemInfo info_;
    std::filesystem::path export_dir_;

    ExperimentForm form_;
    MetricsPanel metrics_;
    HistoryPanel history_panel_;
    CompareView compare_;
    NodeRef<Control> main_page_;

    NodeRef<Label> state_label_;
    NodeRef<Label> system_label_;
    NodeRef<Label> status_label_;

    ExperimentConfig draft_;
    std::vector<ExperimentResult> history_;
    std::string notice_;
    bool notice_is_error_ = false;
    bool compare_visible_ = false;
    bool syncing_compare_ = false;
    std::string compare_primary_id_;
};

}  // namespace

std::unique_ptr<guayaba::Node> build_main_scene(
    IExperimentController& controller, const SystemInfo& info,
    const GuardrailLimits& limits, std::filesystem::path export_dir) {
    auto root = std::make_unique<guayaba::Node>("root");

    NodeRef<Control> screen =
        root->add_child<Control>("screen", guayaba::Vec2{0, 0});
    screen->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    screen->set_clip_children(true);

    // Superficie opaca: el renderer limpia con un color de fondo propio; sin
    // esto, los huecos entre paneles y la cabecera quedan con ese color.
    NodeRef<PanelContainer> background =
        screen->add_child<PanelContainer>("background");
    background->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    background->get_stylebox().set_background_color("BLACK");

    NodeRef<VBox> layout = screen->add_child<VBox>("layout");
    layout->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    layout->set_separation(1);

    // --- Cabecera -----------------------------------------------------------
    NodeRef<HBox> header = add_filling_row(layout, "header", 1);
    header->set_content_alignment(guayaba::ContentAlignment::Begin);
    NodeRef<Label> header_pad = add_fixed_label(header, "header_pad", " ");
    header_pad->set_custom_minimum_size({1, 1});
    add_fixed_label(header, "title", "mem-test");

    NodeRef<Label> state =
        add_fixed_label(header, "state", "Estado: Inactivo");
    NodeRef<Label> system =
        add_reserved_label(header, "system", "Sistema: midiendo...", 1);

    // --- Páginas ------------------------------------------------------------
    // `body` es el contenedor que alterna entre la pantalla principal y la
    // vista de comparación. Ambas comparten cabecera y barra de estado.
    NodeRef<Control> body = layout->add_child<Control>("body");
    body->set_anchor_preset(guayaba::AnchorPreset::FullRect);

    NodeRef<SplitContainer> main_page =
        body->add_child<SplitContainer>("main_page");
    main_page->set_orientation(SplitOrientation::VerticalDivider);
    main_page->set_split_ratio(0.45f);
    main_page->set_min_first(32);
    main_page->set_min_second(34);
    main_page->set_anchor_preset(guayaba::AnchorPreset::FullRect);

    // La prueba de Concurrencia compara con un trabajo total fijo; este es el
    // valor inicial que ve el usuario al abrir la aplicación.
    ExperimentConfig initial;
    initial.work_units = 100000000ull;
    ExperimentForm form =
        ExperimentForm::build(main_page, limits, initial);

    NodeRef<VBox> right = add_filling_box(main_page, "right", 1);
    MetricsPanel metrics = MetricsPanel::build(right);
    HistoryPanel history_panel = HistoryPanel::build(right);

    CompareView compare = CompareView::build(body);

    // --- Barra de estado ----------------------------------------------------
    NodeRef<Label> status =
        add_reserved_label(layout, "status", kDefaultStatus, 1);
    status->set_text_color("WHITE");

    root->attach_script<MainSceneScript>(
        &controller, info, std::move(export_dir), std::move(form),
        std::move(metrics), std::move(history_panel), std::move(compare),
        main_page, state, system, status);

    return root;
}

}  // namespace memtest::ui

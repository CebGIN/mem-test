#include "ui/MainScene.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "ui/ComparePanel.hpp"
#include "ui/ExperimentForm.hpp"
#include "ui/HistoryPanel.hpp"
#include "ui/MetricsPanel.hpp"
#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

using guayaba::Control;
using guayaba::HBox;
using guayaba::InputEvent;
using guayaba::Key;
using guayaba::Label;
using guayaba::NodeRef;
using guayaba::SceneManager;
using guayaba::Script;
using guayaba::SplitContainer;
using guayaba::SplitOrientation;
using guayaba::VBox;

const char* kDefaultStatus =
    "Listo. F5 inicia · F6 detiene · F7 repite · F8 exporta · Esc sale";

class MainSceneScript : public Script<guayaba::Node> {
public:
    MainSceneScript(IExperimentController* controller, SystemInfo info,
                    GuardrailLimits limits, std::filesystem::path export_dir,
                    ExperimentForm form, MetricsPanel metrics,
                    HistoryPanel history_panel, ComparePanel compare,
                    NodeRef<Label> state, NodeRef<Label> guardrails,
                    NodeRef<Label> status)
        : controller_(controller),
          info_(std::move(info)),
          limits_(std::move(limits)),
          export_dir_(std::move(export_dir)),
          form_(std::move(form)),
          metrics_(std::move(metrics)),
          history_panel_(std::move(history_panel)),
          compare_(std::move(compare)),
          state_label_(state),
          guardrails_label_(guardrails),
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
            case Key::ESCAPE:
            case Key::Q:
                SceneManager::get_instance().stop_running();
                break;
            default:
                break;
        }
    }

private:
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
        compare_.set_results(history_);
    }

    void refresh_ui() {
        const LiveState live = controller_->live();
        draft_ = form_.read(draft_);
        const ValidationReport report = controller_->validate(draft_);
        form_.refresh(draft_, report);
        metrics_.refresh(live);
        compare_.refresh();

        const bool running =
            live.state != RunState::Idle && live.state != RunState::Done;
        const bool has_selection = history_panel_.selected_index() >= 0;

        form_.start_button()->set_disabled(running || !report.ok());
        form_.stop_button()->set_disabled(!running);
        form_.repeat_button()->set_disabled(running || !has_selection);
        form_.export_button()->set_disabled(history_.empty());

        if (Label* label = state_label_.get()) {
            label->set_text("Estado: " + state_text(live.state));
        }
        if (Label* label = guardrails_label_.get()) {
            label->set_text(guardrails_text());
        }

        std::string status;
        bool error = false;
        if (!notice_.empty()) {
            status = notice_;
            error = notice_is_error_;
        } else if (!live.message.empty()) {
            status = live.message;
            if (live.state == RunState::Done && !history_.empty()) {
                status += " · " + limits_summary(history_.back());
            }
        } else {
            status = kDefaultStatus;
        }

        if (Label* label = status_label_.get()) {
            label->set_text(status);
            label->set_text_color(error ? "BRIGHT_RED" : "WHITE");
        }
    }

    std::string guardrails_text() const {
        std::ostringstream out;
        out << "RAM " << gib(info_.total_ram) << " · " << std::max(1, info_.cores)
            << " núcleos · " << std::max(1, limits_.max_threads) << " hilos · "
            << std::max(1, limits_.max_wall_timeout_ms) / 1000 << " s · "
            << history_.size() << " ej.";
        return out.str();
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
    GuardrailLimits limits_;
    std::filesystem::path export_dir_;

    ExperimentForm form_;
    MetricsPanel metrics_;
    HistoryPanel history_panel_;
    ComparePanel compare_;

    NodeRef<Label> state_label_;
    NodeRef<Label> guardrails_label_;
    NodeRef<Label> status_label_;

    ExperimentConfig draft_;
    std::vector<ExperimentResult> history_;
    std::string notice_;
    bool notice_is_error_ = false;
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

    NodeRef<VBox> layout = screen->add_child<VBox>("layout");
    layout->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    layout->set_separation(1);

    // --- Cabecera -----------------------------------------------------------
    NodeRef<HBox> header = add_filling_row(layout, "header", 2);
    header->set_content_alignment(guayaba::ContentAlignment::Begin);
    add_fixed_label(header, "title", "mem-test");

    NodeRef<Label> state =
        add_fixed_label(header, "state", "Estado: Inactivo");
    NodeRef<Label> guardrails = add_reserved_label(header, "guardrails", "", 1);

    // --- Cuerpo -------------------------------------------------------------
    NodeRef<SplitContainer> body = layout->add_child<SplitContainer>("body");
    body->set_orientation(SplitOrientation::VerticalDivider);
    body->set_split_ratio(0.45f);
    body->set_min_first(32);
    body->set_min_second(34);
    body->set_anchor_preset(guayaba::AnchorPreset::FullRect);

    ExperimentForm form = ExperimentForm::build(body, limits, ExperimentConfig{});

    NodeRef<VBox> right = add_filling_box(body, "right", 1);
    MetricsPanel metrics = MetricsPanel::build(right);
    HistoryPanel history_panel = HistoryPanel::build(right);
    ComparePanel compare = ComparePanel::build(right);

    // --- Barra de estado ----------------------------------------------------
    NodeRef<Label> status =
        add_reserved_label(layout, "status", kDefaultStatus, 1);
    status->set_text_color("WHITE");

    root->attach_script<MainSceneScript>(
        &controller, info, limits, std::move(export_dir), std::move(form),
        std::move(metrics), std::move(history_panel), std::move(compare), state,
        guardrails, status);

    return root;
}

}  // namespace memtest::ui

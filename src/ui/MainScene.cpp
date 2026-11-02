#include "ui/MainScene.hpp"

#include <cstdint>
#include <deque>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>

namespace memtest::ui {
namespace {

using guayaba::AnchorPreset;
using guayaba::Control;
using guayaba::Grow;
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
using guayaba::StyleBorder;
using guayaba::VBox;

constexpr std::uint64_t kMiB = 1024ull * 1024ull;
constexpr std::uint64_t kGiB = 1024ull * kMiB;

std::string mib(std::uint64_t bytes) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << static_cast<double>(bytes) / static_cast<double>(kMiB) << " MiB";
    return out.str();
}

std::string gib(std::uint64_t bytes) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << static_cast<double>(bytes) / static_cast<double>(kGiB) << " GiB";
    return out.str();
}

std::string state_text(RunState state) {
    switch (state) {
        case RunState::Idle: return "Inactivo";
        case RunState::Validating: return "Validando";
        case RunState::Starting: return "Iniciando";
        case RunState::Running: return "Ejecutando";
        case RunState::Stopping: return "Deteniendo";
        case RunState::Collecting: return "Recolectando";
        case RunState::Done: return "Terminada";
    }
    return "Desconocido";
}

std::string outcome_text(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Completed: return "Completada";
        case RunOutcome::Stopped: return "Detenida";
        case RunOutcome::MemoryLimitHit: return "Límite de memoria";
        case RunOutcome::CpuLimitHit: return "Límite de CPU";
        case RunOutcome::ExternalKill: return "Muerte externa";
        case RunOutcome::Error: return "Error";
    }
    return "Desconocido";
}

void style_panel(const NodeRef<PanelContainer>& panel) {
    guayaba::StyleBox& stylebox = panel->get_stylebox();
    stylebox.set_border_width(1);
    stylebox.set_border_style(StyleBorder::ASCII);
    stylebox.set_border_color("white");
}

// Fila de un VBox: ocupa el ancho de la celda y su altura sale del contenido.
// Un hijo con Grow::Both en el eje Y sería tratado como expansor por el VBox.
NodeRef<Label> add_row_label(const NodeRef<Control>& parent, const std::string& name,
                             const std::string& text) {
    NodeRef<Label> label = parent->add_child<Label>(name);
    label->set_text(text);
    label->set_fit_content(true);
    label->set_anchor_right(1.0f);
    label->set_grow_x(Grow::Both);
    return label;
}

// Texto que ocupa todo el panel (métricas, historial).
NodeRef<Label> add_filling_label(const NodeRef<Control>& parent, const std::string& name,
                                 const std::string& text) {
    NodeRef<Label> label = parent->add_child<Label>(name);
    label->set_text(text);
    label->set_anchor_preset(AnchorPreset::FullRect);
    return label;
}

// Contenedor que ocupa la celda completa que le da su padre.
NodeRef<VBox> add_filling_box(const NodeRef<Control>& parent, const std::string& name,
                              int separation) {
    NodeRef<VBox> box = parent->add_child<VBox>(name);
    box->set_separation(separation);
    box->set_anchor_preset(AnchorPreset::FullRect);
    return box;
}

class MainSceneScript : public Script<guayaba::Node> {
public:
    explicit MainSceneScript(IExperimentController* controller) : controller_(controller) {}

    void bind(NodeRef<Label> state, NodeRef<Label> metrics, NodeRef<Label> history,
              NodeRef<Label> status) {
        state_label_ = state;
        metrics_label_ = metrics;
        history_label_ = history;
        status_label_ = status;
    }

    void on_enter_tree() override { start_run(); }

    void on_process(double) override { refresh(); }

    bool on_input(const InputEvent& event) override {
        if (!event.is_pressed()) {
            return false;
        }
        switch (event.key) {
            case Key::ESCAPE:
            case Key::Q:
                SceneManager::get_instance().stop_running();
                return true;
            case Key::R:
                start_run();
                return true;
            default:
                return false;
        }
    }

private:
    void start_run() {
        if (controller_ == nullptr) {
            return;
        }
        std::string error;
        if (!controller_->start(ExperimentConfig{}, &error)) {
            if (Label* label = status_label_.get()) {
                label->set_text("No se pudo iniciar: " + error);
            }
        }
    }

    void refresh() {
        if (controller_ == nullptr) {
            return;
        }
        const LiveState live = controller_->live();

        if (Label* label = state_label_.get()) {
            label->set_text("Estado: " + state_text(live.state));
        }
        if (Label* label = metrics_label_.get()) {
            label->set_text(metrics_text(live));
        }
        if (Label* label = status_label_.get()) {
            label->set_text(live.message.empty() ? "R: nueva ejecución · Esc: salir"
                                                 : live.message);
        }

        for (const ExperimentResult& result : controller_->take_results()) {
            history_lines_.push_front(format_result(result));
            if (history_lines_.size() > 6) {
                history_lines_.pop_back();
            }
        }
        if (Label* label = history_label_.get()) {
            label->set_text(history_text());
        }
    }

    static std::string metrics_text(const LiveState& live) {
        if (live.state == RunState::Idle && live.recent.empty()) {
            return "Esperando la primera ejecución.\nR inicia una ejecución simulada.";
        }
        const MetricsSnapshot& last = live.last;
        std::ostringstream out;
        out << "RSS:   " << mib(last.vm_rss) << "      VSZ: " << mib(last.vm_size) << '\n';
        out << "Pico:  " << mib(last.vm_hwm) << "      SWAP: "
            << (last.sys_swap_total == 0 ? "no disponible" : mib(last.vm_swap)) << '\n';
        out << "Fallos: " << last.minor_faults << " menores, " << last.major_faults
            << " mayores\n";
        out << "CPU:   " << std::fixed << std::setprecision(2) << last.cpu_user_s << " s usuario, "
            << last.cpu_sys_s << " s sistema\n";
        out << "Hilos: " << last.threads << "      Contexto: "
            << last.voluntary_ctxt + last.involuntary_ctxt;
        return out.str();
    }

    std::string history_text() const {
        if (history_lines_.empty()) {
            return "Sin ejecuciones todavía.";
        }
        std::ostringstream out;
        bool first = true;
        for (const std::string& line : history_lines_) {
            if (!first) {
                out << '\n';
            }
            out << line;
            first = false;
        }
        return out.str();
    }

    static std::string format_result(const ExperimentResult& result) {
        std::ostringstream out;
        out << result.run_id << " · " << outcome_text(result.outcome) << " · " << std::fixed
            << std::setprecision(2)
            << static_cast<double>(result.duration.count()) / 1000.0 << " s · RSS pico "
            << mib(result.peak.vm_rss);
        if (result.expected_value > 0) {
            out << " · obs/esp " << result.observed_value << '/' << result.expected_value;
        }
        return out.str();
    }

    IExperimentController* controller_ = nullptr;
    NodeRef<Label> state_label_;
    NodeRef<Label> metrics_label_;
    NodeRef<Label> history_label_;
    NodeRef<Label> status_label_;
    std::deque<std::string> history_lines_;
};

}  // namespace

std::unique_ptr<guayaba::Node> build_main_scene(IExperimentController& controller,
                                                const SystemInfo& info,
                                                const GuardrailLimits& limits) {
    auto root = std::make_unique<guayaba::Node>("root");

    NodeRef<Control> screen =
        root->add_child<Control>("screen", guayaba::Vec2{0, 0});
    screen->set_anchor_preset(AnchorPreset::FullRect);
    screen->set_clip_children(true);

    NodeRef<VBox> layout = screen->add_child<VBox>("layout");
    layout->set_anchor_preset(AnchorPreset::FullRect);
    layout->set_separation(1);

    // --- Cabecera -----------------------------------------------------------
    NodeRef<HBox> header = layout->add_child<HBox>("header");
    header->set_separation(2);
    header->set_anchor_right(1.0f);
    header->set_grow_x(Grow::Both);

    add_row_label(header, "title", "mem-test");
    NodeRef<Label> state = add_row_label(header, "state", "Estado: Inactivo");
    state->set_grow_x(Grow::Both);

    std::ostringstream guardrails_text;
    guardrails_text << "RAM " << gib(info.total_ram) << " · " << info.cores << " núcleos · "
                    << limits.max_threads << " hilos · " << limits.max_wall_timeout_ms / 1000
                    << " s";
    add_row_label(header, "guardrails", guardrails_text.str());

    // --- Cuerpo: formulario a la izquierda, métricas e historial a la derecha
    NodeRef<SplitContainer> body = layout->add_child<SplitContainer>("body");
    body->set_orientation(SplitOrientation::VerticalDivider);
    body->set_split_ratio(0.42f);
    body->set_min_first(24);
    body->set_min_second(30);
    body->set_anchor_preset(AnchorPreset::FullRect);

    NodeRef<PanelContainer> form_panel = body->add_child<PanelContainer>("form");
    form_panel->set_anchor_preset(AnchorPreset::FullRect);
    style_panel(form_panel);

    NodeRef<VBox> form_box = add_filling_box(form_panel, "form_box", 1);
    add_row_label(form_box, "form_title", "Experimento");
    add_row_label(form_box, "form_kind", "Carga: Concurrencia");
    add_row_label(form_box, "form_threads", "Hilos: 4 · hilos nativos");
    add_row_label(form_box, "form_units", "Unidades: 100000");
    add_row_label(form_box, "form_limits", "Límites: ninguno");
    add_row_label(form_box, "form_note", "WP3 reemplaza este bloque.");

    NodeRef<VBox> right = add_filling_box(body, "right", 1);

    NodeRef<PanelContainer> metrics_panel = right->add_child<PanelContainer>("metrics");
    metrics_panel->set_anchor_preset(AnchorPreset::FullRect);
    metrics_panel->set_custom_minimum_size({0, 8});
    style_panel(metrics_panel);

    NodeRef<PanelContainer> history_panel = right->add_child<PanelContainer>("history");
    history_panel->set_anchor_preset(AnchorPreset::FullRect);
    history_panel->set_custom_minimum_size({0, 8});
    style_panel(history_panel);

    NodeRef<Label> metrics =
        add_filling_label(metrics_panel, "metrics_text", "Esperando la primera ejecución.");
    NodeRef<Label> history =
        add_filling_label(history_panel, "history_text", "Sin ejecuciones todavía.");

    // --- Barra de estado ----------------------------------------------------
    NodeRef<Label> status = add_row_label(layout, "status", "R: nueva ejecución · Esc: salir");

    auto script = root->attach_script<MainSceneScript>(&controller);
    script->bind(state, metrics, history, status);

    return root;
}

}  // namespace memtest::ui

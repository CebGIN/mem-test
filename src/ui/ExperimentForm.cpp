#include "ui/ExperimentForm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

std::uint64_t page_size_bytes() {
    const long page = ::sysconf(_SC_PAGESIZE);
    return page > 0 ? static_cast<std::uint64_t>(page) : 4096ull;
}

using guayaba::Button;
using guayaba::CheckBox;
using guayaba::ContentAlignment;
using guayaba::Control;
using guayaba::HBox;
using guayaba::Label;
using guayaba::ListView;
using guayaba::MarginContainer;
using guayaba::NodeRef;
using guayaba::PanelContainer;
using guayaba::ScrollView;
using guayaba::SpinBox;
using guayaba::VBox;

struct SpinField {
    NodeRef<HBox> row;
    NodeRef<SpinBox> spin;
    NodeRef<Label> label;
};

struct ListField {
    NodeRef<HBox> row;
    NodeRef<ListView> list;
};

int selected_or(const NodeRef<ListView>& list, int fallback) {
    const int index = list->selected();
    return index >= 0 ? index : fallback;
}

// Etiqueta de campo con ancho fijo: alinea todos los controles de la columna.
NodeRef<Label> add_form_label(const NodeRef<Control>& parent, const std::string& name,
                              const std::string& text) {
    NodeRef<Label> label = add_fixed_label(parent, name, text);
    label->set_custom_minimum_size({18, 1});
    return label;
}

SpinField add_spin_field(const NodeRef<Control>& parent, const std::string& name,
                         const std::string& label, double minimum, double maximum,
                         double step, double value) {
    SpinField field;
    field.row = add_filling_row(parent, name + "_row");
    field.row->set_content_alignment(ContentAlignment::Begin);
    field.label = add_form_label(field.row, name + "_label", label);

    field.spin = field.row->add_child<SpinBox>(name);
    field.spin->set_custom_minimum_size({12, 1});
    field.spin->set_background_color("BLACK");
    field.spin->set_range(minimum, maximum);
    field.spin->set_step(step);
    field.spin->set_decimals(0);
    field.spin->set_value(value);
    return field;
}

ListField add_list_field(const NodeRef<Control>& parent, const std::string& name,
                         const std::string& label, std::vector<std::string> items,
                         int minimum_width, int rows) {
    ListField field;
    field.row = add_filling_row(parent, name + "_row");
    field.row->set_content_alignment(ContentAlignment::Begin);
    add_form_label(field.row, name + "_label", label);

    field.list = field.row->add_child<ListView>(name);
    field.list->set_custom_minimum_size({minimum_width, rows});
    field.list->set_items(std::move(items));
    field.list->select_first();
    style_list(field.list);
    return field;
}

void style_tab(const NodeRef<Button>& button, bool selected) {
    using guayaba::ButtonState;
    auto frame = [](guayaba::StyleBox& box, const char* background,
                    const char* border) {
        box.set_background_color(background);
        box.set_border_width(1);
        box.set_border_style(guayaba::StyleBorder::ASCII);
        box.set_border_color(border);
        box.set_content_margin(0, 0, 0, 0);
    };

    frame(button->get_stylebox(ButtonState::Normal),
          selected ? "BLUE" : "BLACK",
          selected ? "BRIGHT_WHITE" : "BRIGHT_BLACK");
    frame(button->get_stylebox(ButtonState::Hover), "CYAN", "WHITE");
    frame(button->get_stylebox(ButtonState::Pressed), "BRIGHT_BLUE",
          "BRIGHT_WHITE");
    frame(button->get_stylebox(ButtonState::Disabled), "BRIGHT_BLACK",
          "BRIGHT_BLACK");
    button->set_text_align(guayaba::TextAlign::Center);
    button->set_text_color(ButtonState::Normal,
                           selected ? "BRIGHT_WHITE" : "WHITE");
    button->set_text_color(ButtonState::Hover, "BLACK");
    button->set_text_color(ButtonState::Pressed, "BRIGHT_WHITE");
    button->set_text_color(ButtonState::Disabled, "WHITE");
}

std::string help_text(WorkloadKind kind) {
    switch (kind) {
        case WorkloadKind::Concurrency:
            return "Reparte el trabajo total entre los hilos y corre hasta "
                   "completarlo o hasta la duración, lo que ocurra primero. "
                   "Hilos nativos es 1:1; tareas runtime usa un pool fijo.";
        case WorkloadKind::CpuLimit:
            return "Carga CPU-bound con 1 hilo. Tiempo total usa RLIMIT_CPU "
                   "(soft=N, hard=N+1); Cuota usa cgroup v2 (Q% de un núcleo). "
                   "En Tiempo total, la duración debe superar N para verlo.";
        case WorkloadKind::MemoryPressure:
            return "Reserva anónima privada y toca una página por vez. Si la "
                   "reserva supera el límite, el resultado esperado es "
                   "MemoryLimitHit.";
        case WorkloadKind::SharedCounter:
            return "Contador compartido durante la duración indicada. Sin "
                   "sincronización usa un contador no atómico con data race "
                   "real; Mutex y Atómico deben ser exactos.";
    }
    return {};
}

}  // namespace

ExperimentForm ExperimentForm::build(const NodeRef<Control>& parent,
                                     const GuardrailLimits& limits,
                                     const ExperimentConfig& initial) {
    ExperimentForm form;
    form.limits_ = limits;

    const std::uint64_t max_memory_mib =
        std::max<std::uint64_t>(1, limits.max_memory_bytes / kMiB);
    const int max_threads = std::max(1, limits.max_threads);
    const int max_wall = std::max(1, limits.max_wall_timeout_ms);
    const int max_cpu = std::max(1, limits.max_cpu_seconds);
    const int max_duration_s = std::max(1, max_wall / 1000 - 2);

    NodeRef<PanelContainer> panel = parent->add_child<PanelContainer>("form");
    panel->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    style_panel(panel);

    NodeRef<VBox> column = add_filling_box(panel, "form_column", 1);

    // El ScrollView dibuja su tirador una columna a la derecha de su rect. El
    // margen interior evita que ese tirador pise el borde del panel.
    NodeRef<MarginContainer> scroll_margin =
        column->add_child<MarginContainer>("form_scroll_margin");
    scroll_margin->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    scroll_margin->set_margins(0, 0, 1, 0);

    NodeRef<ScrollView> scroll = scroll_margin->add_child<ScrollView>("form_scroll");
    scroll->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    scroll->get_track_stylebox().set_background_color("BLACK");
    scroll->get_handle_stylebox().set_background_color("BRIGHT_BLACK");

    NodeRef<VBox> box = add_filling_box(scroll, "form_box", 1);
    box->set_content_alignment(ContentAlignment::Begin);

    // El ListView queda oculto como estado interno del experimento; la
    // selección visible se hace con tabs compactas arriba del formulario.
    form.workload_ = box->add_child<ListView>("workload");
    form.workload_->set_items(workload_items());
    form.workload_->select_first();
    form.workload_->set_visible(false);

    NodeRef<HBox> tabs = add_filling_row(box, "workload_tabs", 1);
    tabs->set_content_alignment(ContentAlignment::Begin);
    const std::array<std::string, 4> tab_labels = {"Conc.", "CPU", "Memoria",
                                                    "Comp."};
    for (int index = 0; index < 4; ++index) {
        NodeRef<Button> tab = tabs->add_child<Button>(
            "workload_tab_" + std::to_string(index));
        tab->set_text(tab_labels[static_cast<std::size_t>(index)]);
        style_button(tab);
        form.workload_tabs_[static_cast<std::size_t>(index)] = tab;
    }

    const ListField worker = add_list_field(
        box, "worker", "Worker", worker_mode_items(), 10, 2);
    form.worker_row_ = worker.row;
    form.worker_mode_ = worker.list;

    const ListField sync = add_list_field(
        box, "sync", "Sincronización", sync_mode_items(), 10, 3);
    form.sync_row_ = sync.row;
    form.sync_mode_ = sync.list;

    const SpinField threads = add_spin_field(
        box, "threads", "Hilos", 1, max_threads, 1,
        static_cast<double>(std::max(1, initial.threads)));
    form.threads_row_ = threads.row;
    form.threads_ = threads.spin;

    const SpinField units = add_spin_field(
        box, "work_units", "Unidades", 1, 1000000000.0, 1000,
        static_cast<double>(std::max<std::uint64_t>(1, initial.work_units)));
    form.work_units_row_ = units.row;
    form.work_units_ = units.spin;
    form.work_units_label_ = units.label;

    const SpinField increments = add_spin_field(
        box, "increments", "Incrementos/tarea", 1, 1000000000.0, 1000,
        static_cast<double>(std::max<std::uint64_t>(1, initial.increments_per_thread)));
    form.increments_row_ = increments.row;
    form.increments_ = increments.spin;

    const std::uint64_t initial_memory_mib =
        initial.memory_bytes > 0 ? initial.memory_bytes / kMiB : 64;
    const SpinField memory = add_spin_field(
        box, "memory", "Memoria (MiB)", 0, static_cast<double>(max_memory_mib), 16,
        static_cast<double>(initial_memory_mib));
    form.memory_row_ = memory.row;
    form.memory_mib_ = memory.spin;

    const SpinField stride = add_spin_field(
        box, "page_stride", "Paso (bytes)", 1, 1048576, 4096,
        static_cast<double>(initial.page_stride_bytes > 0 ? initial.page_stride_bytes
                                                          : 4096));
    form.stride_row_ = stride.row;
    form.page_stride_ = stride.spin;

    form.force_swap_ = box->add_child<CheckBox>("force_swap");
    form.force_swap_->set_text("Forzar SWAP (MADV_PAGEOUT)");
    form.force_swap_->set_background_color(0);
    form.force_swap_->set_checked(initial.force_swap);

    const int initial_duration_s =
        initial.duration_ms > 0
            ? std::max(1, std::min(max_duration_s, initial.duration_ms / 1000))
            : 5;
    const SpinField duration = add_spin_field(
        box, "duration", "Duración (s)", 1, max_duration_s, 1,
        static_cast<double>(initial_duration_s));
    form.duration_s_ = duration.spin;
    form.duration_row_ = duration.row;

    form.cpu_limit_enabled_ = box->add_child<CheckBox>("cpu_limit");
    form.cpu_limit_enabled_->set_text("Límite de CPU (RLIMIT_CPU)");
    form.cpu_limit_enabled_->set_background_color(0);
    form.cpu_limit_enabled_->set_checked(initial.cpu_limit.seconds.has_value());

    const SpinField cpu_seconds = add_spin_field(
        box, "cpu_seconds", "CPU total (s)", 1, max_cpu, 1,
        static_cast<double>(initial.cpu_limit.seconds.value_or(1)));
    form.cpu_seconds_row_ = cpu_seconds.row;
    form.cpu_seconds_ = cpu_seconds.spin;

    const ListField cpu_mode = add_list_field(
        box, "cpu_limit_mode", "Límite", cpu_limit_mode_items(), 14, 2);
    form.cpu_limit_mode_row_ = cpu_mode.row;
    form.cpu_limit_mode_ = cpu_mode.list;

    const SpinField cpu_quota = add_spin_field(
        box, "cpu_quota_percent", "Cuota (%)", 1, 100, 1,
        static_cast<double>(initial.cpu_limit.quota_percent.value_or(50)));
    form.cpu_quota_row_ = cpu_quota.row;
    form.cpu_quota_percent_ = cpu_quota.spin;

    const ListField memory_limit = add_list_field(
        box, "memory_limit", "Límite de memoria", memory_limit_items(), 16, 3);
    form.memory_limit_ = memory_limit.list;

    const std::uint64_t initial_limit_mib =
        initial.memory_limit_bytes > 0 ? initial.memory_limit_bytes / kMiB : 64;
    const SpinField memory_limit_mib = add_spin_field(
        box, "memory_limit_mib", "Límite (MiB)", 1,
        static_cast<double>(max_memory_mib), 16,
        static_cast<double>(initial_limit_mib));
    form.memory_limit_bytes_row_ = memory_limit_mib.row;
    form.memory_limit_mib_ = memory_limit_mib.spin;

    form.message_ = add_reserved_label(column, "form_message", "", 2);
    form.message_->set_text_color("WHITE");

    // Dos filas de dos botones: con borde y padding entran todos a 80 columnas.
    NodeRef<VBox> actions = column->add_child<VBox>("form_actions");
    actions->set_anchor_left(0.0f);
    actions->set_anchor_right(1.0f);
    actions->set_separation(1);

    NodeRef<HBox> primary = add_filling_row(actions, "form_actions_row1", 1);
    primary->set_content_alignment(ContentAlignment::Begin);

    form.start_ = primary->add_child<Button>("start");
    form.start_->set_text("Iniciar");
    style_button(form.start_);

    form.stop_ = primary->add_child<Button>("stop");
    form.stop_->set_text("Detener");
    form.stop_->set_disabled(true);
    style_button(form.stop_);

    NodeRef<HBox> secondary = add_filling_row(actions, "form_actions_row2", 1);
    secondary->set_content_alignment(ContentAlignment::Begin);

    form.repeat_ = secondary->add_child<Button>("repeat");
    form.repeat_->set_text("Repetir");
    form.repeat_->set_disabled(true);
    style_button(form.repeat_);

    form.export_ = secondary->add_child<Button>("export");
    form.export_->set_text("Exportar");
    form.export_->set_disabled(true);
    style_button(form.export_);

    form.write(initial);
    return form;
}

ExperimentConfig ExperimentForm::read(const ExperimentConfig& previous) const {
    ExperimentConfig config = previous;

    const WorkloadKind workload =
        static_cast<WorkloadKind>(selected_or(workload_, 0));
    config.workload = workload;
    // La prueba de limite de CPU usa un solo hilo para que N segundos de CPU
    // coincidan casi con N segundos de pared.
    config.worker_mode =
        (workload == WorkloadKind::CpuLimit ||
         workload == WorkloadKind::MemoryPressure)
            ? WorkerMode::NativeThreads
            : static_cast<WorkerMode>(selected_or(worker_mode_, 0));
    config.sync_mode =
        workload == WorkloadKind::SharedCounter
            ? static_cast<SyncMode>(selected_or(sync_mode_, 0))
            : SyncMode::None;

    config.threads =
        (workload == WorkloadKind::MemoryPressure ||
         workload == WorkloadKind::CpuLimit)
            ? 1
            : std::max(1, static_cast<int>(
                              std::lround(threads_->get_value())));

    const int duration_s =
        std::max(1, static_cast<int>(std::lround(duration_s_->get_value())));
    config.duration_ms = duration_s * 1000;
    config.wall_timeout_ms = std::min(
        limits_.max_wall_timeout_ms, config.duration_ms + 2000);

    // Concurrencia usa un trabajo total fijo; el resto de las cargas corre
    // por duración. Los incrementos historicos quedan siempre en cero.
    config.load_mode = workload == WorkloadKind::Concurrency
                           ? LoadMode::TotalWork
                           : LoadMode::Duration;
    config.work_units =
        workload == WorkloadKind::Concurrency
            ? std::max<std::uint64_t>(
                  1, static_cast<std::uint64_t>(
                         std::llround(work_units_->get_value())))
            : 0;
    config.increments_per_thread = 0;

    if (workload == WorkloadKind::MemoryPressure) {
        config.memory_bytes =
            static_cast<std::uint64_t>(std::max(0.0, memory_mib_->get_value())) * kMiB;
        // El paso queda fijo al tamano real de pagina; el campo se conserva
        // oculto solo por compatibilidad con configuraciones viejas.
        config.page_stride_bytes = page_size_bytes();
        config.force_swap = force_swap_->is_checked();
        // En memoria no se aplica limite de CPU: podria cortar la carga antes
        // de tocar todas las paginas.
        config.cpu_limit.seconds.reset();
        config.cpu_limit.quota_percent.reset();
    } else {
        config.memory_bytes = 0;
        config.force_swap = false;
    }

    if (workload == WorkloadKind::CpuLimit) {
        const int cpu_mode = selected_or(cpu_limit_mode_, 0);
        if (cpu_mode == 1) {
            config.cpu_limit.quota_percent = std::clamp(
                static_cast<int>(std::lround(cpu_quota_percent_->get_value())),
                1, 100);
            config.cpu_limit.seconds.reset();
        } else {
            config.cpu_limit.seconds = std::max(
                1, static_cast<int>(std::lround(cpu_seconds_->get_value())));
            config.cpu_limit.quota_percent.reset();
        }
    } else if (workload != WorkloadKind::MemoryPressure &&
               cpu_limit_enabled_->is_checked()) {
        config.cpu_limit.seconds =
            std::max(1, static_cast<int>(std::lround(cpu_seconds_->get_value())));
        config.cpu_limit.quota_percent.reset();
    } else {
        config.cpu_limit.seconds.reset();
        config.cpu_limit.quota_percent.reset();
    }

    config.memory_limit_kind =
        static_cast<MemoryLimitKind>(selected_or(memory_limit_, 0));
    if (config.memory_limit_kind == MemoryLimitKind::None) {
        config.memory_limit_bytes = 0;
    } else {
        config.memory_limit_bytes = static_cast<std::uint64_t>(
            std::max(1.0, memory_limit_mib_->get_value())) * kMiB;
    }

    return config;
}

void ExperimentForm::write(const ExperimentConfig& config) {
    workload_->set_selected(static_cast<int>(config.workload));
    worker_mode_->set_selected(static_cast<int>(config.worker_mode));
    sync_mode_->set_selected(static_cast<int>(config.sync_mode));

    threads_->set_value(std::max(1, config.threads));
    work_units_->set_value(
        static_cast<double>(std::max<std::uint64_t>(1, config.work_units)));
    increments_->set_value(
        static_cast<double>(std::max<std::uint64_t>(1, config.increments_per_thread)));
    memory_mib_->set_value(static_cast<double>(config.memory_bytes / kMiB));
    page_stride_->set_value(static_cast<double>(
        config.page_stride_bytes > 0 ? config.page_stride_bytes : 4096));
    force_swap_->set_checked(config.force_swap);
    duration_s_->set_value(std::max(
        1, config.duration_ms > 0 ? config.duration_ms / 1000
                                  : config.wall_timeout_ms / 1000));

    cpu_limit_mode_->set_selected(
        config.cpu_limit.quota_percent.has_value() ? 1 : 0);
    cpu_limit_enabled_->set_checked(config.cpu_limit.seconds.has_value());
    cpu_seconds_->set_value(config.cpu_limit.seconds.value_or(1));
    cpu_quota_percent_->set_value(
        config.cpu_limit.quota_percent.value_or(50));

    memory_limit_->set_selected(static_cast<int>(config.memory_limit_kind));
    const std::uint64_t limit_mib =
        config.memory_limit_bytes > 0 ? config.memory_limit_bytes / kMiB : 64;
    memory_limit_mib_->set_value(static_cast<double>(limit_mib));

    refresh(config, ValidationReport{});
}

NodeRef<Button> ExperimentForm::workload_button(int index) const {
    if (index < 0 || index >= static_cast<int>(workload_tabs_.size())) {
        return {};
    }
    return workload_tabs_[static_cast<std::size_t>(index)];
}

void ExperimentForm::set_workload(WorkloadKind kind) {
    workload_->set_selected(static_cast<int>(kind));

    // Presets por experimento: dejan la variable de interes preparada y
    // evitan que una prueba herede parametros de otra.
    if (kind == WorkloadKind::Concurrency) {
        if (work_units_->get_value() < 1.0) {
            work_units_->set_value(100000000.0);
        }
        threads_->set_value(4);
        worker_mode_->select_first();
        cpu_limit_enabled_->set_checked(false);
        cpu_limit_mode_->select_first();
        memory_limit_->select_first();  // sin limite de memoria
        force_swap_->set_checked(false);
        duration_s_->set_value(5);
    } else if (kind == WorkloadKind::CpuLimit) {
        cpu_limit_enabled_->set_checked(true);
        cpu_limit_mode_->select_first();
        cpu_seconds_->set_value(5);
        cpu_quota_percent_->set_value(50);
        duration_s_->set_value(10);
        threads_->set_value(1);
        worker_mode_->select_first();
        memory_limit_->select_first();  // sin limite de memoria
        force_swap_->set_checked(false);
    } else if (kind == WorkloadKind::MemoryPressure) {
        memory_mib_->set_value(128);
        page_stride_->set_value(
            static_cast<double>(page_size_bytes()));
        force_swap_->set_checked(true);
        memory_limit_->set_selected(1);  // RLIMIT_AS
        memory_limit_mib_->set_value(256);
        duration_s_->set_value(5);
        cpu_limit_enabled_->set_checked(false);
        cpu_limit_mode_->select_first();
    } else if (kind == WorkloadKind::SharedCounter) {
        threads_->set_value(4);
        worker_mode_->select_first();
        duration_s_->set_value(5);
        cpu_limit_enabled_->set_checked(false);
        cpu_limit_mode_->select_first();
        memory_limit_->select_first();  // sin limite de memoria
        force_swap_->set_checked(false);
    }
}

void ExperimentForm::refresh(const ExperimentConfig& config,
                             const ValidationReport& report) {
    const WorkloadKind workload = config.workload;
    for (std::size_t index = 0; index < workload_tabs_.size(); ++index) {
        style_tab(workload_tabs_[index],
                  static_cast<std::size_t>(workload) == index);
    }
    const bool cpu_test = workload == WorkloadKind::CpuLimit;
    const bool uses_threads =
        workload != WorkloadKind::MemoryPressure && !cpu_test;

    worker_row_->set_visible(uses_threads);
    threads_row_->set_visible(uses_threads);
    sync_row_->set_visible(workload == WorkloadKind::SharedCounter);
    // Concurrencia usa trabajo total; el resto corre por duración. Los
    // incrementos historicos quedan siempre ocultos.
    increments_row_->set_visible(false);
    work_units_row_->set_visible(workload == WorkloadKind::Concurrency);
    if (work_units_label_) {
        work_units_label_->set_text(workload == WorkloadKind::Concurrency
                                        ? "Trabajo total"
                                        : "Unidades");
    }
    duration_row_->set_visible(true);
    memory_row_->set_visible(workload == WorkloadKind::MemoryPressure);
    stride_row_->set_visible(false);
    force_swap_->set_visible(workload == WorkloadKind::MemoryPressure);
    const int cpu_mode = selected_or(cpu_limit_mode_, 0);
    cpu_limit_enabled_->set_visible(!cpu_test &&
                                    workload != WorkloadKind::MemoryPressure);
    cpu_limit_mode_row_->set_visible(cpu_test);
    cpu_seconds_row_->set_visible(
        cpu_test ? cpu_mode == 0
                 : (workload != WorkloadKind::MemoryPressure &&
                    cpu_limit_enabled_->is_checked()));
    cpu_quota_row_->set_visible(cpu_test && cpu_mode == 1);
    memory_limit_bytes_row_->set_visible(config.memory_limit_kind != MemoryLimitKind::None);

    if (!report.ok()) {
        std::string message;
        for (const ValidationIssue& issue : report.issues) {
            if (!message.empty()) {
                message += " · ";
            }
            message += issue.field + ": " + issue.message;
        }
        message_->set_text(message);
        message_->set_text_color("BRIGHT_RED");
        return;
    }

    std::string help = help_text(workload);
    if (config.cpu_limit.seconds.has_value() && config.duration_ms > 0 &&
        *config.cpu_limit.seconds * 1000 >= config.duration_ms) {
        help += " La duración no supera el límite de CPU; subila para verlo actuar.";
    }
    if (workload == WorkloadKind::MemoryPressure &&
        config.memory_limit_kind != MemoryLimitKind::None &&
        config.memory_limit_bytes > 0) {
        if (config.memory_bytes > config.memory_limit_bytes) {
            help += " La reserva supera el límite: se espera MemoryLimitHit.";
        } else if (config.memory_bytes + 16 * kMiB >
                   config.memory_limit_bytes) {
            help += " El límite incluye la línea base del worker; dejá margen.";
        }
    }
    message_->set_text(help);
    message_->set_text_color("WHITE");
}

}  // namespace memtest::ui

#include "ui/ExperimentForm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

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
    add_form_label(field.row, name + "_label", label);

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

std::string help_text(WorkloadKind kind) {
    switch (kind) {
        case WorkloadKind::Concurrency:
            return "Carga de hilos con unidades de trabajo. Hilos nativos es 1:1; "
                   "tareas runtime reparte N tareas en un pool fijo.";
        case WorkloadKind::CpuLimit:
            return "Carga CPU-bound. RLIMIT_CPU usa soft=N y hard=N+1; el resultado "
                   "esperado es límite de CPU.";
        case WorkloadKind::MemoryPressure:
            return "Reserva anónima privada y toca páginas. El paso se expresa en "
                   "bytes y debe ser múltiplo de página.";
        case WorkloadKind::SharedCounter:
            return "Contador compartido. Sin sincronización se pierden "
                   "actualizaciones; Mutex y Atómico deben ser exactos.";
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

    // Selector de carga.
    add_fixed_label(box, "workload_label", "Experimento");
    form.workload_ = box->add_child<ListView>("workload");
    form.workload_->set_custom_minimum_size({30, 4});
    form.workload_->set_items(workload_items());
    form.workload_->select_first();
    style_list(form.workload_);

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

    const SpinField wall = add_spin_field(
        box, "wall", "Duración (ms)", 1, max_wall, 500,
        static_cast<double>(std::max(1, initial.wall_timeout_ms)));
    form.wall_ms_ = wall.spin;

    form.cpu_limit_enabled_ = box->add_child<CheckBox>("cpu_limit");
    form.cpu_limit_enabled_->set_text("Límite de CPU (RLIMIT_CPU)");
    form.cpu_limit_enabled_->set_background_color(0);
    form.cpu_limit_enabled_->set_checked(initial.cpu_limit.seconds.has_value());

    const SpinField cpu_seconds = add_spin_field(
        box, "cpu_seconds", "CPU (s)", 1, max_cpu, 1,
        static_cast<double>(initial.cpu_limit.seconds.value_or(1)));
    form.cpu_seconds_row_ = cpu_seconds.row;
    form.cpu_seconds_ = cpu_seconds.spin;

    NodeRef<Label> cpu_quota =
        add_label(box, "cpu_quota", "Cuota CPU (cgroup): no soportada");
    cpu_quota->set_text_color("BRIGHT_BLACK");

    const ListField memory_limit = add_list_field(
        box, "memory_limit", "Límite de memoria", memory_limit_items(), 10, 3);
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

    NodeRef<HBox> actions = add_filling_row(column, "form_actions", 1);
    actions->set_content_alignment(ContentAlignment::Begin);

    form.start_ = actions->add_child<Button>("start");
    form.start_->set_text("Iniciar");
    style_button(form.start_);

    form.stop_ = actions->add_child<Button>("stop");
    form.stop_->set_text("Detener");
    form.stop_->set_disabled(true);
    style_button(form.stop_);

    form.repeat_ = actions->add_child<Button>("repeat");
    form.repeat_->set_text("Repetir");
    form.repeat_->set_disabled(true);
    style_button(form.repeat_);

    form.export_ = actions->add_child<Button>("export");
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
    config.worker_mode =
        static_cast<WorkerMode>(selected_or(worker_mode_, 0));
    config.sync_mode = static_cast<SyncMode>(selected_or(sync_mode_, 0));

    config.threads = workload == WorkloadKind::MemoryPressure
                         ? 1
                         : std::max(1, static_cast<int>(
                                           std::lround(threads_->get_value())));
    config.wall_timeout_ms =
        std::max(1, static_cast<int>(std::lround(wall_ms_->get_value())));

    if (workload == WorkloadKind::MemoryPressure) {
        config.memory_bytes =
            static_cast<std::uint64_t>(std::max(0.0, memory_mib_->get_value())) * kMiB;
        config.page_stride_bytes = static_cast<std::uint64_t>(
            std::max(1.0, page_stride_->get_value()));
    } else {
        config.memory_bytes = 0;
    }

    if (workload == WorkloadKind::SharedCounter) {
        config.increments_per_thread = static_cast<std::uint64_t>(
            std::max(1.0, increments_->get_value()));
    }
    if (workload == WorkloadKind::Concurrency || workload == WorkloadKind::CpuLimit) {
        config.work_units = static_cast<std::uint64_t>(
            std::max(1.0, work_units_->get_value()));
    }

    if (cpu_limit_enabled_->is_checked()) {
        config.cpu_limit.seconds =
            std::max(1, static_cast<int>(std::lround(cpu_seconds_->get_value())));
    } else {
        config.cpu_limit.seconds.reset();
    }
    // La cuota cgroup no se configura en el MVP; se conserva la anterior.

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
    wall_ms_->set_value(std::max(1, config.wall_timeout_ms));

    cpu_limit_enabled_->set_checked(config.cpu_limit.seconds.has_value());
    cpu_seconds_->set_value(config.cpu_limit.seconds.value_or(1));

    memory_limit_->set_selected(static_cast<int>(config.memory_limit_kind));
    const std::uint64_t limit_mib =
        config.memory_limit_bytes > 0 ? config.memory_limit_bytes / kMiB : 64;
    memory_limit_mib_->set_value(static_cast<double>(limit_mib));

    refresh(config, ValidationReport{});
}

void ExperimentForm::refresh(const ExperimentConfig& config,
                             const ValidationReport& report) {
    const WorkloadKind workload = config.workload;
    const bool uses_threads = workload != WorkloadKind::MemoryPressure;

    worker_row_->set_visible(uses_threads);
    threads_row_->set_visible(uses_threads);
    sync_row_->set_visible(workload == WorkloadKind::SharedCounter);
    increments_row_->set_visible(workload == WorkloadKind::SharedCounter);
    work_units_row_->set_visible(workload == WorkloadKind::Concurrency ||
                                 workload == WorkloadKind::CpuLimit);
    memory_row_->set_visible(workload == WorkloadKind::MemoryPressure);
    stride_row_->set_visible(workload == WorkloadKind::MemoryPressure);
    cpu_seconds_row_->set_visible(cpu_limit_enabled_->is_checked());
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

    message_->set_text(help_text(workload));
    message_->set_text_color("WHITE");
}

}  // namespace memtest::ui

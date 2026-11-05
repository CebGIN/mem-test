#ifndef MEMTEST_UI_EXPERIMENTFORM_HPP
#define MEMTEST_UI_EXPERIMENTFORM_HPP

#include <array>
#include <string>

#include <guayaba/Guayaba.hpp>

#include "domain/Types.hpp"
#include "domain/Validation.hpp"

namespace memtest::ui {

// Formulario adaptativo de WP3. No tiene lógica de negocio: lee y escribe
// ExperimentConfig y muestra el resultado de la validación pura de domain/.
// Las filas que no aplican a la carga elegida se ocultan, sin destruir nodos.
class ExperimentForm {
public:
    static ExperimentForm build(const guayaba::NodeRef<guayaba::Control>& parent,
                                const GuardrailLimits& limits,
                                const ExperimentConfig& initial);

    // Parte de `previous` y pisa solo los campos que esta carga usa, para no
    // arrastrar valores irrelevantes al historial.
    ExperimentConfig read(const ExperimentConfig& previous) const;

    // Carga una configuración (por ejemplo, al pulsar Repetir).
    void write(const ExperimentConfig& config);

    // Adapta la visibilidad de las filas y muestra errores o ayuda.
    void refresh(const ExperimentConfig& config, const ValidationReport& report);

    // Tabs superiores de experimento.
    guayaba::NodeRef<guayaba::Button> workload_button(int index) const;
    void set_workload(WorkloadKind kind);

    guayaba::NodeRef<guayaba::Button> start_button() const { return start_; }
    guayaba::NodeRef<guayaba::Button> stop_button() const { return stop_; }
    guayaba::NodeRef<guayaba::Button> repeat_button() const { return repeat_; }
    guayaba::NodeRef<guayaba::Button> export_button() const { return export_; }

private:
    std::array<guayaba::NodeRef<guayaba::Button>, 4> workload_tabs_;
    guayaba::NodeRef<guayaba::ListView> workload_;
    guayaba::NodeRef<guayaba::ListView> worker_mode_;
    guayaba::NodeRef<guayaba::ListView> sync_mode_;
    guayaba::NodeRef<guayaba::ListView> cpu_limit_mode_;
    guayaba::NodeRef<guayaba::ListView> memory_limit_;
    guayaba::NodeRef<guayaba::SpinBox> threads_;
    guayaba::NodeRef<guayaba::SpinBox> work_units_;
    guayaba::NodeRef<guayaba::SpinBox> increments_;
    guayaba::NodeRef<guayaba::SpinBox> memory_mib_;
    guayaba::NodeRef<guayaba::SpinBox> page_stride_;
    guayaba::NodeRef<guayaba::SpinBox> duration_s_;
    guayaba::NodeRef<guayaba::CheckBox> cpu_limit_enabled_;
    guayaba::NodeRef<guayaba::CheckBox> force_swap_;
    guayaba::NodeRef<guayaba::SpinBox> cpu_seconds_;
    guayaba::NodeRef<guayaba::SpinBox> cpu_quota_percent_;
    guayaba::NodeRef<guayaba::SpinBox> memory_limit_mib_;

    guayaba::NodeRef<guayaba::Label> work_units_label_;
    guayaba::NodeRef<guayaba::HBox> worker_row_;
    guayaba::NodeRef<guayaba::HBox> sync_row_;
    guayaba::NodeRef<guayaba::HBox> cpu_limit_mode_row_;
    guayaba::NodeRef<guayaba::HBox> threads_row_;
    guayaba::NodeRef<guayaba::HBox> work_units_row_;
    guayaba::NodeRef<guayaba::HBox> increments_row_;
    guayaba::NodeRef<guayaba::HBox> memory_row_;
    guayaba::NodeRef<guayaba::HBox> stride_row_;
    guayaba::NodeRef<guayaba::HBox> duration_row_;
    guayaba::NodeRef<guayaba::HBox> cpu_seconds_row_;
    guayaba::NodeRef<guayaba::HBox> cpu_quota_row_;
    guayaba::NodeRef<guayaba::HBox> memory_limit_bytes_row_;

    guayaba::NodeRef<guayaba::Label> message_;

    guayaba::NodeRef<guayaba::Button> start_;
    guayaba::NodeRef<guayaba::Button> stop_;
    guayaba::NodeRef<guayaba::Button> repeat_;
    guayaba::NodeRef<guayaba::Button> export_;

    GuardrailLimits limits_;
};

}  // namespace memtest::ui

#endif  // MEMTEST_UI_EXPERIMENTFORM_HPP

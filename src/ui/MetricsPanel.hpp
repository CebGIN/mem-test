#ifndef MEMTEST_UI_METRICSPANEL_HPP
#define MEMTEST_UI_METRICSPANEL_HPP

#include <guayaba/Guayaba.hpp>

#include "app/IExperimentController.hpp"

namespace memtest::ui {

// Panel de sistema en vivo. Muestra métricas globales (CPU, RAM, SWAP, load,
// fallos y context switches) y, debajo, las métricas del worker de la carga
// actual. No conoce metrics/ ni process/: solo el contrato de app/.
class MetricsPanel {
public:
    static MetricsPanel build(const guayaba::NodeRef<guayaba::Control>& parent);

    void refresh(const LiveState& live);

private:
    guayaba::NodeRef<guayaba::Label> system_values_;
    guayaba::NodeRef<guayaba::Label> worker_values_;
    guayaba::NodeRef<guayaba::Graph> graph_;      // CPU del sistema
    guayaba::NodeRef<guayaba::Graph> mem_graph_;  // métrica seleccionable
    guayaba::NodeRef<guayaba::ListView> metric_;
};

}  // namespace memtest::ui

#endif  // MEMTEST_UI_METRICSPANEL_HPP

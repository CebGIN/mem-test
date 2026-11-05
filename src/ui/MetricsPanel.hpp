#ifndef MEMTEST_UI_METRICSPANEL_HPP
#define MEMTEST_UI_METRICSPANEL_HPP

#include <guayaba/Guayaba.hpp>

#include "app/IExperimentController.hpp"

namespace memtest::ui {

// Panel de métricas en vivo: etiquetas con unidades y una gráfica sobre
// LiveState.recent. No conoce métricas/ ni process/: solo el contrato de app/.
class MetricsPanel {
public:
    static MetricsPanel build(const guayaba::NodeRef<guayaba::Control>& parent);

    void refresh(const LiveState& live);

private:
    guayaba::NodeRef<guayaba::Label> values_;
    guayaba::NodeRef<guayaba::Label> hint_;
    guayaba::NodeRef<guayaba::Graph> graph_;
    guayaba::NodeRef<guayaba::ListView> metric_;
};

}  // namespace memtest::ui

#endif  // MEMTEST_UI_METRICSPANEL_HPP

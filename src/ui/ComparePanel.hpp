#ifndef MEMTEST_UI_COMPAREPANEL_HPP
#define MEMTEST_UI_COMPAREPANEL_HPP

#include <cstddef>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "domain/Types.hpp"

namespace memtest::ui {

// Comparación sobre todo el historial: ordena por el parámetro elegido y
// grafica/tabula una métrica. La selección del historial no interviene acá.
class ComparePanel {
public:
    static ComparePanel build(const guayaba::NodeRef<guayaba::Control>& parent);

    void set_results(const std::vector<ExperimentResult>& results);

    // Recalcula si cambió el eje, la métrica o el historial.
    void refresh();

private:
    guayaba::NodeRef<guayaba::ListView> parameter_;
    guayaba::NodeRef<guayaba::ListView> metric_;
    guayaba::NodeRef<guayaba::Graph> graph_;
    guayaba::NodeRef<guayaba::Label> table_;

    std::vector<ExperimentResult> results_;
    int last_parameter_ = -1;
    int last_metric_ = -1;
    std::size_t last_size_ = 0;
};

}  // namespace memtest::ui

#endif  // MEMTEST_UI_COMPAREPANEL_HPP

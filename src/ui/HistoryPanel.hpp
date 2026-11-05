#ifndef MEMTEST_UI_HISTORYPANEL_HPP
#define MEMTEST_UI_HISTORYPANEL_HPP

#include <vector>

#include <guayaba/Guayaba.hpp>

#include "domain/Types.hpp"

namespace memtest::ui {

// Historial de la sesión. Una fila por ejecución; la selección se usa para
// Repetir y para exportar el detalle. No guarda resultados por su cuenta.
class HistoryPanel {
public:
    static HistoryPanel build(const guayaba::NodeRef<guayaba::Control>& parent);

    void set_results(const std::vector<ExperimentResult>& results);

    int selected_index() const;
    void select_index(int index);
    void select_last();

    guayaba::NodeRef<guayaba::ListView> list() const { return list_; }
    guayaba::NodeRef<guayaba::Button> compare_button() const {
        return compare_;
    }

private:
    guayaba::NodeRef<guayaba::Label> title_;
    guayaba::NodeRef<guayaba::Label> placeholder_;
    guayaba::NodeRef<guayaba::ListView> list_;
    guayaba::NodeRef<guayaba::Button> compare_;
};

}  // namespace memtest::ui

#endif  // MEMTEST_UI_HISTORYPANEL_HPP

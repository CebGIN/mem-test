#ifndef MEMTEST_UI_COMPAREVIEW_HPP
#define MEMTEST_UI_COMPAREVIEW_HPP

#include <string>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "domain/Types.hpp"

namespace memtest::ui {

// Vista de comparación A/B. Ocupa el cuerpo completo de la escena: a la
// izquierda la ficha de la ejecución elegida en el historial, a la derecha un
// selector con las demás corridas y la ficha de la segunda. No conoce el
// controlador: recibe el historial ya medido.
class CompareView {
public:
    static CompareView build(const guayaba::NodeRef<guayaba::Control>& parent);

    // Reemplaza el historial y fija A por `primary_id`. B se conserva si sigue
    // existiendo; si no, se elige la corrida más reciente distinta de A.
    void set_results(const std::vector<ExperimentResult>& results,
                     const std::string& primary_id);

    // El usuario movió el cursor del selector: actualiza B y su ficha.
    void select_secondary(int index);

    std::string secondary_id() const { return secondary_id_; }
    const ExperimentResult* primary() const;

    guayaba::NodeRef<guayaba::Control> page() const { return page_; }
    guayaba::NodeRef<guayaba::ListView> list() const { return list_; }
    guayaba::NodeRef<guayaba::Button> back_button() const { return back_; }
    guayaba::NodeRef<guayaba::Button> repeat_button() const { return repeat_; }

private:
    const ExperimentResult* find(const std::string& run_id) const;
    void refresh_details();
    void size_selector(std::size_t rows);

    std::vector<ExperimentResult> results_;
    std::string primary_id_;
    std::string secondary_id_;
    // run_id de cada fila del selector, en el mismo orden que los items.
    std::vector<std::string> row_ids_;

    guayaba::NodeRef<guayaba::Control> page_;
    guayaba::NodeRef<guayaba::Label> primary_badge_;
    guayaba::NodeRef<guayaba::Label> primary_title_;
    guayaba::NodeRef<guayaba::Label> primary_details_;
    guayaba::NodeRef<guayaba::Label> selector_title_;
    guayaba::NodeRef<guayaba::Label> secondary_badge_;
    guayaba::NodeRef<guayaba::Label> secondary_title_;
    guayaba::NodeRef<guayaba::Label> secondary_details_;
    guayaba::NodeRef<guayaba::ListView> list_;
    guayaba::NodeRef<guayaba::Button> back_;
    guayaba::NodeRef<guayaba::Button> repeat_;
};

}  // namespace memtest::ui

#endif  // MEMTEST_UI_COMPAREVIEW_HPP

#include "ui/HistoryPanel.hpp"

#include <string>
#include <utility>
#include <vector>

#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

using guayaba::Button;
using guayaba::Control;
using guayaba::HBox;
using guayaba::Label;
using guayaba::ListView;
using guayaba::NodeRef;
using guayaba::PanelContainer;
using guayaba::VBox;

}  // namespace

HistoryPanel HistoryPanel::build(const NodeRef<Control>& parent) {
    HistoryPanel panel;

    NodeRef<PanelContainer> container = parent->add_child<PanelContainer>("history");
    container->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    style_panel(container);

    NodeRef<VBox> box = add_filling_box(container, "history_box", 0);

    panel.title_ = add_label(box, "history_title", "Historial");

    panel.placeholder_ = add_label(box, "history_placeholder", "Sin ejecuciones todavía.");
    panel.placeholder_->set_text_color("BRIGHT_BLACK");

    panel.list_ = box->add_child<ListView>("history_list");
    panel.list_->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    panel.list_->set_custom_minimum_size({0, 1});
    style_list(panel.list_);

    // Acción principal del panel: abrir la vista de comparación A/B con la
    // fila seleccionada.
    NodeRef<HBox> actions = add_filling_row(box, "history_actions", 0);
    actions->set_content_alignment(guayaba::ContentAlignment::Begin);
    panel.compare_ = actions->add_child<Button>("history_compare");
    panel.compare_->set_text("Comparar");
    panel.compare_->set_disabled(true);
    style_button(panel.compare_);

    return panel;
}

void HistoryPanel::set_results(const std::vector<ExperimentResult>& results) {
    std::vector<std::string> rows;
    rows.reserve(results.size());
    for (const ExperimentResult& result : results) {
        rows.push_back(history_row(result));
    }

    list_->clear_item_colors();
    for (std::size_t index = 0; index < results.size(); ++index) {
        list_->set_item_color(static_cast<int>(index),
                              guayaba::Color::from_name(
                                  outcome_color(results[index].outcome)));
    }
    list_->set_items(std::move(rows));
    list_->set_visible(!results.empty());
    placeholder_->set_visible(results.empty());

    if (results.empty()) {
        title_->set_text("Historial");
    } else {
        title_->set_text("Historial · " + std::to_string(results.size()) +
                         (results.size() == 1 ? " ejecución" : " ejecuciones"));
    }
}

int HistoryPanel::selected_index() const {
    return list_->selected();
}

void HistoryPanel::select_index(int index) {
    list_->set_selected(index);
}

void HistoryPanel::select_last() {
    list_->select_last();
}

}  // namespace memtest::ui

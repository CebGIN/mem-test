#include "ui/HistoryPanel.hpp"

#include <string>
#include <utility>
#include <vector>

#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

using guayaba::Control;
using guayaba::Grow;
using guayaba::Label;
using guayaba::ListView;
using guayaba::NodeRef;
using guayaba::PanelContainer;
using guayaba::VBox;

const char* color_for(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Completed: return guayaba::Color::GREEN;
        case RunOutcome::Stopped: return guayaba::Color::CYAN;
        case RunOutcome::MemoryLimitHit:
        case RunOutcome::CpuLimitHit: return guayaba::Color::YELLOW;
        case RunOutcome::ExternalKill:
        case RunOutcome::Error: return guayaba::Color::RED;
    }
    return guayaba::Color::WHITE;
}

}  // namespace

HistoryPanel HistoryPanel::build(const NodeRef<Control>& parent) {
    HistoryPanel panel;

    NodeRef<PanelContainer> container = parent->add_child<PanelContainer>("history");
    container->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    style_panel(container);

    NodeRef<VBox> box = add_filling_box(container, "history_box", 0);

    panel.title_ = add_label(box, "history_title", "Historial · sin ejecuciones");

    panel.list_ = box->add_child<ListView>("history_list");
    panel.list_->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    panel.list_->set_custom_minimum_size({0, 1});

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
                              guayaba::Color::from_name(color_for(results[index].outcome)));
    }
    list_->set_items(std::move(rows));

    if (results.empty()) {
        title_->set_text("Historial · sin ejecuciones");
    } else {
        title_->set_text("Historial · " + std::to_string(results.size()) +
                         (results.size() == 1 ? " ejecución" : " ejecuciones"));
    }
}

int HistoryPanel::selected_index() const {
    return list_->selected();
}

void HistoryPanel::select_last() {
    list_->select_last();
}

}  // namespace memtest::ui

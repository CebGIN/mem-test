#include "ui/CompareView.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

using guayaba::AnchorPreset;
using guayaba::Button;
using guayaba::ContentAlignment;
using guayaba::Control;
using guayaba::Grow;
using guayaba::HBox;
using guayaba::Label;
using guayaba::ListView;
using guayaba::NodeRef;
using guayaba::PanelContainer;
using guayaba::SplitContainer;
using guayaba::SplitOrientation;
using guayaba::VBox;

// Filas del cuerpo de la ficha, sin el título. A 80×24 el panel derecho tiene
// 18 filas: título del selector + hasta 4 corridas + título de B + 12 campos.
constexpr int kDetailLines = 12;
constexpr int kMaxSelectorRows = 4;

NodeRef<Label> add_badge(const NodeRef<Control>& parent, const std::string& name,
                         const std::string& text, const char* background,
                         const char* foreground) {
    NodeRef<Label> badge = add_fixed_label(parent, name, text);
    badge->set_background_color(background);
    badge->set_text_color(foreground);
    return badge;
}

}  // namespace

CompareView CompareView::build(const NodeRef<Control>& parent) {
    CompareView view;

    NodeRef<SplitContainer> page =
        parent->add_child<SplitContainer>("compare_page");
    page->set_orientation(SplitOrientation::VerticalDivider);
    page->set_split_ratio(0.5f);
    page->set_min_first(30);
    page->set_min_second(34);
    page->set_anchor_preset(AnchorPreset::FullRect);
    // La escena alterna main_page/compare_page; arranca oculta.
    page->set_visible(false);
    view.page_ = page;

    // --- Ejecución A --------------------------------------------------------
    NodeRef<PanelContainer> left =
        page->add_child<PanelContainer>("compare_left");
    left->set_anchor_preset(AnchorPreset::FullRect);
    style_panel(left);

    NodeRef<VBox> left_box = add_filling_box(left, "compare_left_box", 0);

    // La cabecera identifica la corrida con una insignia y la primera línea de
    // la ficha; el cuerpo no la repite.
    NodeRef<HBox> left_header = add_filling_row(left_box, "compare_left_header", 1);
    left_header->set_content_alignment(ContentAlignment::Begin);
    view.primary_badge_ =
        add_badge(left_header, "compare_left_badge", " A ", "BLUE", "BRIGHT_WHITE");
    view.primary_title_ =
        add_label(left_header, "compare_left_title", "Sin ejecución");
    view.primary_title_->set_text_color("WHITE");

    NodeRef<HBox> actions = add_filling_row(left_box, "compare_actions", 1);
    actions->set_content_alignment(ContentAlignment::Begin);
    view.back_ = actions->add_child<Button>("compare_back");
    view.back_->set_text("Volver");
    style_button(view.back_);
    view.repeat_ = actions->add_child<Button>("compare_repeat");
    view.repeat_->set_text("Repetir A");
    view.repeat_->set_disabled(true);
    style_button(view.repeat_);

    view.primary_details_ = add_reserved_label(
        left_box, "compare_primary_details", "", kDetailLines, true);
    view.primary_details_->set_text_color("WHITE");

    // --- Ejecución B --------------------------------------------------------
    NodeRef<PanelContainer> right =
        page->add_child<PanelContainer>("compare_right");
    right->set_anchor_preset(AnchorPreset::FullRect);
    style_panel(right);

    NodeRef<VBox> right_box = add_filling_box(right, "compare_right_box", 0);
    view.selector_title_ =
        add_fixed_label(right_box, "compare_right_title", "Comparar con");

    // El selector tiene alto propio: crece con la cantidad de corridas hasta un
    // tope, y la ficha de B queda pegada debajo. Con FullRect, el VBox le daba
    // todo el espacio sobrante y abría un hueco en el medio del panel.
    view.list_ = right_box->add_child<ListView>("compare_selector");
    view.list_->set_anchor_left(0.0f);
    view.list_->set_anchor_right(1.0f);
    view.list_->set_anchor_top(0.0f);
    view.list_->set_anchor_bottom(0.0f);
    view.list_->set_grow_x(Grow::Both);
    view.list_->set_custom_minimum_size({0, 1});
    style_list(view.list_);

    NodeRef<HBox> right_header =
        add_filling_row(right_box, "compare_right_header", 1);
    right_header->set_content_alignment(ContentAlignment::Begin);
    view.secondary_badge_ = add_badge(right_header, "compare_right_badge", " B ",
                                      "CYAN", "BLACK");
    view.secondary_title_ =
        add_label(right_header, "compare_secondary_title", "Sin segunda ejecución");
    view.secondary_title_->set_text_color("BRIGHT_BLACK");

    view.secondary_details_ = add_reserved_label(
        right_box, "compare_secondary_details", "", kDetailLines, true);
    view.secondary_details_->set_text_color("WHITE");

    return view;
}

void CompareView::size_selector(std::size_t rows) {
    if (rows == 0) {
        list_->set_custom_minimum_size({0, 0});
        return;
    }
    const std::size_t visible =
        std::min(rows, static_cast<std::size_t>(kMaxSelectorRows));
    list_->set_custom_minimum_size({0, static_cast<int>(visible)});
}

void CompareView::set_results(const std::vector<ExperimentResult>& results,
                              const std::string& primary_id) {
    results_ = results;
    primary_id_ = primary_id;

    std::vector<std::string> rows;
    row_ids_.clear();
    for (const ExperimentResult& result : results_) {
        if (result.run_id == primary_id_) {
            continue;
        }
        row_ids_.push_back(result.run_id);
        rows.push_back(history_row(result));
    }

    list_->clear_item_colors();
    for (std::size_t row = 0; row < row_ids_.size(); ++row) {
        const ExperimentResult* result = find(row_ids_[row]);
        if (result != nullptr) {
            list_->set_item_color(static_cast<int>(row),
                                  guayaba::Color::from_name(
                                      outcome_color(result->outcome)));
        }
    }

    list_->set_items(rows);
    list_->set_visible(!rows.empty());
    size_selector(rows.size());

    selector_title_->set_text("Comparar con · " + std::to_string(rows.size()));

    // Conserva B si sigue existiendo; si no, la corrida más reciente.
    int index = -1;
    for (std::size_t row = 0; row < row_ids_.size(); ++row) {
        if (row_ids_[row] == secondary_id_) {
            index = static_cast<int>(row);
            break;
        }
    }
    if (index < 0 && !row_ids_.empty()) {
        index = static_cast<int>(row_ids_.size()) - 1;
    }
    secondary_id_ = index >= 0 ? row_ids_[static_cast<std::size_t>(index)]
                               : std::string();
    list_->set_selected(index);

    refresh_details();
}

void CompareView::select_secondary(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= row_ids_.size()) {
        return;
    }
    secondary_id_ = row_ids_[static_cast<std::size_t>(index)];
    refresh_details();
}

const ExperimentResult* CompareView::primary() const {
    return find(primary_id_);
}

const ExperimentResult* CompareView::find(const std::string& run_id) const {
    if (run_id.empty()) {
        return nullptr;
    }
    for (const ExperimentResult& result : results_) {
        if (result.run_id == run_id) {
            return &result;
        }
    }
    return nullptr;
}

void CompareView::refresh_details() {
    const ExperimentResult* first = find(primary_id_);
    if (first == nullptr) {
        primary_title_->set_text("Sin ejecución seleccionada");
        primary_title_->set_text_color("WHITE");
        primary_details_->set_text("Elegí una corrida del historial.");
        repeat_->set_disabled(true);
    } else {
        primary_title_->set_text(detail_title(*first));
        primary_title_->set_text_color(outcome_color(first->outcome));
        primary_details_->set_text(detail_fields(*first));
        repeat_->set_disabled(false);
    }

    const ExperimentResult* second = find(secondary_id_);
    if (second == nullptr) {
        secondary_badge_->set_visible(false);
        if (row_ids_.empty()) {
            secondary_title_->set_text("Sin otra ejecución");
            secondary_title_->set_text_color("BRIGHT_BLACK");
            secondary_details_->set_text(
                "Agregá otra corrida al historial para comparar.");
        } else {
            secondary_title_->set_text("Elegí una ejecución de la lista");
            secondary_title_->set_text_color("BRIGHT_BLACK");
            secondary_details_->set_text(
                "Usá ↑↓ o hacé clic en una fila del selector.");
        }
    } else {
        secondary_badge_->set_visible(true);
        secondary_title_->set_text(detail_title(*second));
        secondary_title_->set_text_color(outcome_color(second->outcome));
        secondary_details_->set_text(detail_fields(*second, first));
    }
}

}  // namespace memtest::ui

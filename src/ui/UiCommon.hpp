#ifndef MEMTEST_UI_UICOMMON_HPP
#define MEMTEST_UI_UICOMMON_HPP

#include <string>

#include <guayaba/Guayaba.hpp>

namespace memtest::ui {

inline void style_panel(const guayaba::NodeRef<guayaba::PanelContainer>& panel) {
    guayaba::StyleBox& box = panel->get_stylebox();
    box.set_background_color("BLACK");
    box.set_border_width(1);
    box.set_border_style(guayaba::StyleBorder::ASCII);
    box.set_border_color("BRIGHT_BLACK");
    box.set_content_margin(1, 0, 1, 0);
}

inline void style_list(const guayaba::NodeRef<guayaba::ListView>& list) {
    list->set_text_color("WHITE");
    list->set_selection_background_color("BLUE");
    list->set_selection_text_color("BRIGHT_WHITE");
}

inline void style_button_box(guayaba::StyleBox& box, const char* background,
                             const char* border) {
    box.set_background_color(background);
    box.set_border_width(1);
    box.set_border_style(guayaba::StyleBorder::ASCII);
    box.set_border_color(border);
    box.set_content_margin(1, 0, 1, 0);
}

// Botón de acción: fondo, borde y un margen horizontal para que el texto no
// toque el marco. El borde eleva la altura mínima a tres filas.
inline void style_button(const guayaba::NodeRef<guayaba::Button>& button) {
    using guayaba::ButtonState;
    style_button_box(button->get_stylebox(ButtonState::Normal), "BLUE",
                     "BRIGHT_WHITE");
    style_button_box(button->get_stylebox(ButtonState::Hover), "CYAN", "WHITE");
    style_button_box(button->get_stylebox(ButtonState::Pressed),
                     "BRIGHT_BLUE", "BRIGHT_WHITE");
    style_button_box(button->get_stylebox(ButtonState::Disabled),
                     "BRIGHT_BLACK", "BRIGHT_BLACK");
    button->set_text_align(guayaba::TextAlign::Center);
    button->set_text_color(ButtonState::Normal, "BRIGHT_WHITE");
    button->set_text_color(ButtonState::Hover, "BLACK");
    button->set_text_color(ButtonState::Pressed, "BRIGHT_WHITE");
    button->set_text_color(ButtonState::Disabled, "WHITE");
}

// Etiqueta de una línea que reserva el ancho de su texto. Es la fila típica de
// un HBox: sin fit_content un Label no aporta mínimo y la celda sale vacía.
inline guayaba::NodeRef<guayaba::Label> add_label(
    const guayaba::NodeRef<guayaba::Control>& parent, const std::string& name,
    const std::string& text) {
    guayaba::NodeRef<guayaba::Label> label = parent->add_child<guayaba::Label>(name);
    label->set_text(text);
    label->set_fit_content(true);
    label->set_anchor_right(1.0f);
    label->set_grow_x(guayaba::Grow::Both);
    label->set_clip_children(true);
    return label;
}

// Etiqueta que solo ocupa el ancho de su texto. Es la adecuada dentro de un
// HBox de campo, donde el SpinBox o el ListView debe quedarse con el resto.
inline guayaba::NodeRef<guayaba::Label> add_fixed_label(
    const guayaba::NodeRef<guayaba::Control>& parent, const std::string& name,
    const std::string& text) {
    guayaba::NodeRef<guayaba::Label> label = parent->add_child<guayaba::Label>(name);
    label->set_text(text);
    label->set_fit_content(true);
    label->set_clip_children(true);
    return label;
}

// Etiqueta de bloque: no mide su texto, pero reserva `lines` filas. Sirve para
// paneles que crecen y para textos que se recortan en pantallas chicas.
inline guayaba::NodeRef<guayaba::Label> add_reserved_label(
    const guayaba::NodeRef<guayaba::Control>& parent, const std::string& name,
    const std::string& text, int lines, bool grow = false) {
    guayaba::NodeRef<guayaba::Label> label = parent->add_child<guayaba::Label>(name);
    label->set_text(text);
    label->set_custom_minimum_size({0, lines});
    label->set_clip_children(true);
    if (grow) {
        label->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    } else {
        label->set_anchor_right(1.0f);
        label->set_grow_x(guayaba::Grow::Both);
    }
    return label;
}

inline guayaba::NodeRef<guayaba::VBox> add_filling_box(
    const guayaba::NodeRef<guayaba::Control>& parent, const std::string& name,
    int separation = 1) {
    guayaba::NodeRef<guayaba::VBox> box = parent->add_child<guayaba::VBox>(name);
    box->set_separation(separation);
    box->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    return box;
}

inline guayaba::NodeRef<guayaba::HBox> add_filling_row(
    const guayaba::NodeRef<guayaba::Control>& parent, const std::string& name,
    int separation = 1) {
    guayaba::NodeRef<guayaba::HBox> row = parent->add_child<guayaba::HBox>(name);
    row->set_separation(separation);
    row->set_anchor_left(0.0f);
    row->set_anchor_right(1.0f);
    row->set_anchor_top(0.0f);
    row->set_anchor_bottom(0.0f);
    row->set_grow_x(guayaba::Grow::Both);
    return row;
}

}  // namespace memtest::ui

#endif  // MEMTEST_UI_UICOMMON_HPP

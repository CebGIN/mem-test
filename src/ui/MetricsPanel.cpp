#include "ui/MetricsPanel.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

using guayaba::ContentAlignment;
using guayaba::Control;
using guayaba::Graph;
using guayaba::HBox;
using guayaba::Label;
using guayaba::ListView;
using guayaba::NodeRef;
using guayaba::PanelContainer;
using guayaba::VBox;

std::string swap_text(const MetricsSnapshot& sample) {
    if (sample.sys_swap_total == 0) {
        return "no disponible";
    }
    return mib(sample.vm_swap);
}

}  // namespace

MetricsPanel MetricsPanel::build(const NodeRef<Control>& parent) {
    MetricsPanel panel;

    NodeRef<PanelContainer> container = parent->add_child<PanelContainer>("metrics");
    container->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    style_panel(container);

    NodeRef<VBox> box = add_filling_box(container, "metrics_box", 0);

    NodeRef<HBox> header = add_filling_row(box, "metrics_header", 2);
    header->set_content_alignment(ContentAlignment::Begin);
    add_fixed_label(header, "metrics_title", "Métricas");

    panel.metric_ = header->add_child<ListView>("metrics_metric");
    panel.metric_->set_custom_minimum_size({12, 1});
    panel.metric_->set_items({"RSS (MiB)", "VSZ (MiB)", "CPU (s)", "Fallos"});
    panel.metric_->select_first();
    style_list(panel.metric_);

    panel.values_ = add_reserved_label(box, "metrics_values", "", 3);
    panel.values_->set_text("Esperando la primera ejecución.");

    panel.graph_empty_ =
        add_reserved_label(box, "metrics_graph_empty", "Esperando muestras...", 1, true);
    panel.graph_empty_->set_text_color("BRIGHT_BLACK");

    panel.graph_ = box->add_child<Graph>("metrics_graph");
    panel.graph_->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    panel.graph_->set_custom_minimum_size({0, 1});
    panel.graph_->set_capacity(120);
    panel.graph_->set_line_color("CYAN");
    panel.graph_->set_fill(true);

    panel.hint_ = add_reserved_label(box, "metrics_hint", "esperando muestras", 1);
    panel.hint_->set_text_color("BRIGHT_BLACK");
    return panel;
}

void MetricsPanel::refresh(const LiveState& live) {
    const MetricsSnapshot& last = live.last;
    const bool has_samples = !live.recent.empty();

    if (!has_samples) {
        graph_->set_values({});
        graph_->set_visible(false);
        graph_empty_->set_visible(true);
        hint_->set_text("");
        if (live.state == RunState::Done) {
            values_->set_text("La ejecución terminó sin muestras.");
            graph_empty_->set_text("La ejecución no produjo muestras.");
        } else {
            values_->set_text("Esperando la primera ejecución.\nF5 inicia · F8 exporta");
            graph_empty_->set_text("Esperando muestras...");
        }
        return;
    }
    graph_->set_visible(true);
    graph_empty_->set_visible(false);

    std::ostringstream values;
    values << "RSS " << mib(last.vm_rss) << "  Pico " << mib(last.vm_hwm) << '\n';
    values << "VSZ " << mib(last.vm_size) << "  SWAP " << swap_text(last) << '\n';
    values << "Fallos " << last.minor_faults << '/' << last.major_faults << "  CPU "
           << seconds_text(last.cpu_user_s + last.cpu_sys_s) << "  Hilos "
           << last.threads;
    values_->set_text(values.str());

    std::ostringstream hint;
    hint << "Serie: " << live.recent.size() << " muestras · últ. "
         << seconds_text(static_cast<double>(last.elapsed.count()) / 1000.0);
    hint_->set_text(hint.str());

    const int metric = metric_->selected() >= 0 ? metric_->selected() : 0;
    std::vector<double> samples;
    samples.reserve(live.recent.size());
    for (const MetricsSnapshot& sample : live.recent) {
        switch (metric) {
            case 0:
                samples.push_back(static_cast<double>(sample.vm_rss) /
                                  static_cast<double>(kMiB));
                break;
            case 1:
                samples.push_back(static_cast<double>(sample.vm_size) /
                                  static_cast<double>(kMiB));
                break;
            case 2:
                samples.push_back(sample.cpu_user_s + sample.cpu_sys_s);
                break;
            default:
                samples.push_back(static_cast<double>(sample.minor_faults));
                break;
        }
    }
    graph_->set_values(std::move(samples));
}

}  // namespace memtest::ui

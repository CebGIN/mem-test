#include "ui/ComparePanel.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ui/UiCommon.hpp"
#include "ui/UiFormat.hpp"

namespace memtest::ui {
namespace {

using guayaba::ContentAlignment;
using guayaba::Control;
using guayaba::Graph;
using guayaba::Grow;
using guayaba::HBox;
using guayaba::Label;
using guayaba::ListView;
using guayaba::NodeRef;
using guayaba::PanelContainer;
using guayaba::VBox;

constexpr std::size_t kMaxRows = 8;

double parameter_value(const ExperimentResult& result, int parameter) {
    switch (parameter) {
        case 1:
            return static_cast<double>(result.config.memory_bytes) /
                   static_cast<double>(kMiB);
        case 2:
            return static_cast<double>(result.config.cpu_limit.seconds.value_or(0));
        default:
            return static_cast<double>(result.config.threads);
    }
}

std::string parameter_name(int parameter) {
    switch (parameter) {
        case 1: return "Mem MiB";
        case 2: return "CPU s";
        default: return "Hilos";
    }
}

std::string parameter_value_text(const ExperimentResult& result, int parameter) {
    switch (parameter) {
        case 1:
            return std::to_string(result.config.memory_bytes / kMiB);
        case 2:
            return std::to_string(result.config.cpu_limit.seconds.value_or(0));
        default:
            return std::to_string(result.config.threads);
    }
}

double metric_value(const ExperimentResult& result, int metric) {
    switch (metric) {
        case 1:
            return result.peak.cpu_user_s + result.peak.cpu_sys_s;
        case 2:
            return static_cast<double>(result.peak.minor_faults);
        case 3:
            return static_cast<double>(result.duration.count()) / 1000.0;
        default:
            return static_cast<double>(result.peak.vm_rss) / static_cast<double>(kMiB);
    }
}

std::string metric_name(int metric) {
    switch (metric) {
        case 1: return "CPU s";
        case 2: return "Fallos";
        case 3: return "Duración s";
        default: return "RSS MiB";
    }
}

std::string metric_value_text(const ExperimentResult& result, int metric) {
    switch (metric) {
        case 1:
            return seconds_text(result.peak.cpu_user_s + result.peak.cpu_sys_s);
        case 2:
            return std::to_string(result.peak.minor_faults);
        case 3:
            return duration_text(result.duration);
        default:
            return mib(result.peak.vm_rss);
    }
}

}  // namespace

ComparePanel ComparePanel::build(const NodeRef<Control>& parent) {
    ComparePanel panel;

    NodeRef<PanelContainer> container = parent->add_child<PanelContainer>("compare");
    container->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    style_panel(container);

    NodeRef<VBox> box = add_filling_box(container, "compare_box", 0);

    NodeRef<HBox> header = add_filling_row(box, "compare_header", 1);
    header->set_content_alignment(ContentAlignment::Begin);
    add_fixed_label(header, "compare_axis_label", "Eje");

    panel.parameter_ = header->add_child<ListView>("compare_parameter");
    panel.parameter_->set_custom_minimum_size({11, 1});
    panel.parameter_->set_anchor_right(1.0f);
    panel.parameter_->set_grow_x(Grow::Both);
    panel.parameter_->set_items({"Hilos", "Memoria MiB", "CPU s"});
    panel.parameter_->select_first();

    add_fixed_label(header, "compare_metric_label", "Métrica");

    panel.metric_ = header->add_child<ListView>("compare_metric");
    panel.metric_->set_custom_minimum_size({12, 1});
    panel.metric_->set_anchor_right(1.0f);
    panel.metric_->set_grow_x(Grow::Both);
    panel.metric_->set_items({"RSS pico MiB", "CPU s", "Fallos", "Duración s"});
    panel.metric_->select_first();

    panel.graph_ = box->add_child<Graph>("compare_graph");
    panel.graph_->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    panel.graph_->set_custom_minimum_size({0, 1});
    panel.graph_->set_line_color("MAGENTA");

    panel.table_ = add_reserved_label(box, "compare_table", "", 1, true);
    panel.table_->set_text("Sin ejecuciones para comparar.");

    return panel;
}

void ComparePanel::set_results(const std::vector<ExperimentResult>& results) {
    results_ = results;
    last_size_ = static_cast<std::size_t>(-1);
    refresh();
}

void ComparePanel::refresh() {
    const int parameter = parameter_->selected() >= 0 ? parameter_->selected() : 0;
    const int metric = metric_->selected() >= 0 ? metric_->selected() : 0;
    if (parameter == last_parameter_ && metric == last_metric_ &&
        results_.size() == last_size_) {
        return;
    }
    last_parameter_ = parameter;
    last_metric_ = metric;
    last_size_ = results_.size();

    if (results_.empty()) {
        graph_->set_values({});
        table_->set_text("Sin ejecuciones para comparar.");
        return;
    }

    // Se comparan las últimas ejecuciones para que la tabla quepa; la gráfica
    // usa la misma ventana y queda ordenada de menor a mayor parámetro.
    const std::size_t begin =
        results_.size() > kMaxRows ? results_.size() - kMaxRows : 0;
    std::vector<std::size_t> order;
    order.reserve(results_.size() - begin);
    for (std::size_t index = begin; index < results_.size(); ++index) {
        order.push_back(index);
    }
    std::stable_sort(order.begin(), order.end(),
                     [this, parameter](std::size_t a, std::size_t b) {
                         return parameter_value(results_[a], parameter) <
                                parameter_value(results_[b], parameter);
                     });

    std::vector<double> samples;
    samples.reserve(order.size());
    for (const std::size_t index : order) {
        samples.push_back(metric_value(results_[index], metric));
    }
    graph_->set_values(std::move(samples));

    std::ostringstream table;
    table << std::left << std::setw(7) << parameter_name(parameter) << " "
          << std::setw(10) << metric_name(metric) << " Resultado\n";
    for (const std::size_t index : order) {
        const ExperimentResult& result = results_[index];
        table << std::left << std::setw(7) << parameter_value_text(result, parameter) << " "
              << std::setw(10) << metric_value_text(result, metric) << " "
              << outcome_text(result.outcome) << '\n';
    }
    std::string text = table.str();
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    table_->set_text(text);
}

}  // namespace memtest::ui

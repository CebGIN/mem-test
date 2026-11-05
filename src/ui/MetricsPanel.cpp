#include "ui/MetricsPanel.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
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

std::string percent_text(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << value << "%";
    return out.str();
}

std::string rate_text(std::uint64_t value) {
    std::ostringstream out;
    if (value >= 1000) {
        out << std::fixed << std::setprecision(1)
            << static_cast<double>(value) / 1000.0 << "k";
    } else {
        out << value;
    }
    return out.str();
}

std::string gib_pair(std::uint64_t used, std::uint64_t total) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << static_cast<double>(used) / static_cast<double>(kGiB) << "/"
        << static_cast<double>(total) / static_cast<double>(kGiB) << " GiB";
    return out.str();
}

std::string swap_text(const SystemSnapshot& system) {
    if (!system.swap_available) {
        return "no disponible";
    }
    return gib_pair(system.swap_used, system.swap_total);
}

std::string delta_text(std::uint64_t value) {
    if (value == 0) {
        return "";
    }
    return " (+" + mib(value) + ")";
}

}  // namespace

MetricsPanel MetricsPanel::build(const NodeRef<Control>& parent) {
    MetricsPanel panel;

    NodeRef<PanelContainer> container = parent->add_child<PanelContainer>("metrics");
    container->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    style_panel(container);

    NodeRef<VBox> box = add_filling_box(container, "metrics_box", 0);

    NodeRef<HBox> header = add_filling_row(box, "metrics_header", 1);
    header->set_content_alignment(ContentAlignment::Begin);
    add_fixed_label(header, "metrics_title", "Sistema");

    panel.metric_ = header->add_child<ListView>("metrics_metric");
    panel.metric_->set_custom_minimum_size({10, 1});
    panel.metric_->set_items({"RAM", "SWAP", "Fallos/s", "RSS carga"});
    panel.metric_->select_first();
    style_list(panel.metric_);

    panel.system_values_ = add_reserved_label(
        box, "metrics_values", "Sistema: midiendo...", 2);
    panel.system_values_->set_text_color("WHITE");

    panel.graph_ = box->add_child<Graph>("metrics_graph");
    panel.graph_->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    panel.graph_->set_custom_minimum_size({0, 1});
    panel.graph_->set_capacity(240);
    panel.graph_->set_line_color("CYAN");
    panel.graph_->set_fill(true);
    panel.graph_->set_range(0.0, 100.0);

    panel.mem_graph_ = box->add_child<Graph>("metrics_mem_graph");
    panel.mem_graph_->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    panel.mem_graph_->set_custom_minimum_size({0, 1});
    panel.mem_graph_->set_capacity(240);
    panel.mem_graph_->set_line_color("GREEN");
    panel.mem_graph_->set_fill(true);

    panel.worker_values_ =
        add_reserved_label(box, "metrics_worker", "", 1);
    panel.worker_values_->set_text_color("WHITE");
    return panel;
}

void MetricsPanel::refresh(const LiveState& live) {
    const SystemSnapshot& system = live.system;
    const bool has_system = system.mem_total > 0;

    if (!has_system) {
        system_values_->set_text("Sistema: midiendo...");
        graph_->set_values({});
        graph_->set_visible(false);
        mem_graph_->set_values({});
        mem_graph_->set_visible(false);
    } else {
        std::ostringstream values;
        values << "CPU " << percent_text(system.cpu_total_percent) << " · RAM "
               << gib_pair(system.mem_used, system.mem_total);
        if (live.has_system_baseline &&
            system.mem_used > live.system_baseline.mem_used) {
            values << delta_text(system.mem_used - live.system_baseline.mem_used);
        }
        values << '\n';
        values << "SWAP " << swap_text(system) << " · L " << std::fixed
               << std::setprecision(1) << system.load_1m << " · Fall "
               << rate_text(system.minor_faults_per_second) << "/"
               << rate_text(system.major_faults_per_second);
        system_values_->set_text(values.str());

        std::vector<double> cpu_values;
        cpu_values.reserve(live.system_recent.size());
        for (const SystemSnapshot& sample : live.system_recent) {
            cpu_values.push_back(sample.cpu_total_percent);
        }
        graph_->set_values(std::move(cpu_values));
        graph_->set_visible(!live.system_recent.empty());

        const int metric = metric_->selected() >= 0 ? metric_->selected() : 0;
        std::vector<double> values_series;
        double maximum = 1.0;
        const char* color = "GREEN";
        if (metric == 0) {
            values_series.reserve(live.system_recent.size());
            for (const SystemSnapshot& sample : live.system_recent) {
                values_series.push_back(static_cast<double>(sample.mem_used) /
                                        static_cast<double>(kMiB));
            }
            maximum = static_cast<double>(system.mem_total) /
                      static_cast<double>(kMiB);
            color = "GREEN";
            mem_graph_->set_range(0.0, std::max(1.0, maximum));
        } else if (metric == 1) {
            color = "YELLOW";
            if (system.swap_available) {
                values_series.reserve(live.system_recent.size());
                for (const SystemSnapshot& sample : live.system_recent) {
                    values_series.push_back(
                        static_cast<double>(sample.swap_used) /
                        static_cast<double>(kMiB));
                }
                maximum = static_cast<double>(system.swap_total) /
                          static_cast<double>(kMiB);
                mem_graph_->set_range(0.0, std::max(1.0, maximum));
            }
        } else if (metric == 2) {
            color = "MAGENTA";
            values_series.reserve(live.system_recent.size());
            for (const SystemSnapshot& sample : live.system_recent) {
                values_series.push_back(static_cast<double>(
                    sample.minor_faults_per_second +
                    sample.major_faults_per_second));
            }
            mem_graph_->clear_range();
        } else {
            color = "CYAN";
            values_series.reserve(live.recent.size());
            for (const MetricsSnapshot& sample : live.recent) {
                values_series.push_back(static_cast<double>(sample.vm_rss) /
                                        static_cast<double>(kMiB));
            }
            mem_graph_->clear_range();
        }
        mem_graph_->set_line_color(color);
        mem_graph_->set_values(std::move(values_series));
        mem_graph_->set_visible(!mem_graph_->get_values().empty());
    }

    if (live.recent.empty()) {
        worker_values_->set_text(
            "Carga: sin ejecución.\nF5 inicia · F6 detiene · F7 repite · F8 exporta");
        return;
    }

    const MetricsSnapshot& last = live.last;
    std::ostringstream worker;
    worker << "Carga RSS " << mib(last.vm_rss) << " · VSZ " << mib(last.vm_size)
           << " · SWAP "
           << (last.sys_swap_total == 0 ? std::string("no disp")
                                        : mib(last.vm_swap))
           << " · " << last.threads << " hilos · Fallos " << last.minor_faults
           << " men/" << last.major_faults << " may · CPU "
           << seconds_text(last.cpu_user_s + last.cpu_sys_s);
    worker_values_->set_text(worker.str());
}

}  // namespace memtest::ui

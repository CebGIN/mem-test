#include "metrics/MetricsSourceFactory.hpp"

#include "metrics/ProcMetricsSource.hpp"

namespace memtest {

std::unique_ptr<IMetricsSource> make_metrics_source() {
    return std::make_unique<metrics::ProcMetricsSource>();
}

}  // namespace memtest

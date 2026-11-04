#include "metrics/MetricsSourceFactory.hpp"

#if defined(_WIN32)
#include "metrics/WinMetricsSource.hpp"
#else
#include "metrics/ProcMetricsSource.hpp"
#endif

namespace memtest {

std::unique_ptr<IMetricsSource> make_metrics_source() {
#if defined(_WIN32)
    return std::make_unique<metrics::WinMetricsSource>();
#else
    return std::make_unique<metrics::ProcMetricsSource>();
#endif
}

}  // namespace memtest

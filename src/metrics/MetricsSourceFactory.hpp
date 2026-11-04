#ifndef MEMTEST_METRICS_METRICSSOURCEFACTORY_HPP
#define MEMTEST_METRICS_METRICSSOURCEFACTORY_HPP

#include <memory>

#include "metrics/IMetricsSource.hpp"

namespace memtest {

// Devuelve la fuente de métricas de la plataforma actual (ProcMetricsSource en
// Linux, WinMetricsSource en Windows). El resto del código no necesita saber
// cuál es.
std::unique_ptr<IMetricsSource> make_metrics_source();

}  // namespace memtest

#endif  // MEMTEST_METRICS_METRICSSOURCEFACTORY_HPP

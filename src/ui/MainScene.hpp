#ifndef MEMTEST_UI_MAINSCENE_HPP
#define MEMTEST_UI_MAINSCENE_HPP

#include <memory>

#include "app/IExperimentController.hpp"
#include "domain/Types.hpp"
#include "domain/Validation.hpp"

namespace guayaba {
class Node;
}  // namespace guayaba

namespace memtest::ui {

// Construye el esqueleto de la escena principal de WP0: cabecera, formulario
// mock, panel de métricas, historial y barra de estado. La escena solo conoce
// IExperimentController y tipos de domain/; los workers los maneja el
// controlador. WP3 reemplaza el mock por el formulario real.
std::unique_ptr<guayaba::Node> build_main_scene(IExperimentController& controller,
                                                const SystemInfo& info,
                                                const GuardrailLimits& limits);

}  // namespace memtest::ui

#endif  // MEMTEST_UI_MAINSCENE_HPP

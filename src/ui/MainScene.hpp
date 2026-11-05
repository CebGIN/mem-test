#ifndef MEMTEST_UI_MAINSCENE_HPP
#define MEMTEST_UI_MAINSCENE_HPP

#include <filesystem>
#include <memory>

#include "app/IExperimentController.hpp"
#include "domain/Types.hpp"
#include "domain/Validation.hpp"

namespace guayaba {
class Node;
}  // namespace guayaba

namespace memtest::ui {

// Construye la escena principal de WP3: formulario adaptativo, métricas con
// gráfica, historial, comparación y barra de estado. La escena solo conoce
// IExperimentController y domain/; no crea procesos ni lee /proc.
std::unique_ptr<guayaba::Node> build_main_scene(
    IExperimentController& controller, const SystemInfo& info,
    const GuardrailLimits& limits,
    std::filesystem::path export_dir = "exportaciones");

}  // namespace memtest::ui

#endif  // MEMTEST_UI_MAINSCENE_HPP

#include "ui/MainScene.hpp"

namespace memtest::ui {

MainScene::MainScene(std::shared_ptr<app::IExperimentController> controller)
    : controller_(controller) {
}

void MainScene::on_init() {
    // Inicialización de la escena principal
}

void MainScene::on_update(float delta_time) {
    // Actualización por fotograma
}

} 
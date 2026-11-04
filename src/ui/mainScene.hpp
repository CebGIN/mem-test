#ifndef MEMTEST_UI_MAIN_SCENE_HPP
#define MEMTEST_UI_MAIN_SCENE_HPP

#include <guayaba/Guayaba.hpp>
#include "app/IExperimentController.hpp"
#include "domain/Types.hpp"
#include <memory>

namespace memtest::ui {

class MainScene : public guayaba::Scene {
public:
    explicit MainScene(std::shared_ptr<app::IExperimentController> controller);
    ~MainScene() override = default;

    void on_init() override;
    void on_update(float delta_time) override;

private:
    std::shared_ptr<app::IExperimentController> controller_;
};

} // namespace memtest::ui

#endif 
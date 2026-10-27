#include <iostream>

#include <guayaba/Guayaba.hpp>

int main() {
    // Referencia al motor para comprobar que la libreria quedo enlazada.
    const auto frames = guayaba::SceneManager::get_instance().get_frame_count();
    std::cout << "GuayabaTUIEngine listo (frames: " << frames << ").\n";
    return 0;
}

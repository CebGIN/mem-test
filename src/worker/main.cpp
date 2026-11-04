#include <iostream>
#include <string>

#include <unistd.h>

// Punto de entrada del proceso hijo. En WP0 solo cierra el protocolo con un
// error estructurado: los límites, el runtime y los workloads reales llegan en
// WP1, WP2 y WP4. El padre lee la configuración por stdin y los eventos por
// stdout; al worker se lo controla con señales.
int main() {
    std::string config_line;
    std::getline(std::cin, config_line);  // una línea y el padre cierra el pipe

    std::cout << "HELLO worker=1 pid=" << ::getpid() << '\n';
    std::cout << "ERROR code=not_implemented detail=\"worker pendiente (WP4)\"\n";
    std::cout << "END\n" << std::flush;

    return 13;  // error del worker (Arquitectura.md §7.3)
}

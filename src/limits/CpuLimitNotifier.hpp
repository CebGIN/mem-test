#ifndef MEMTEST_LIMITS_CPULIMITNOTIFIER_HPP
#define MEMTEST_LIMITS_CPULIMITNOTIFIER_HPP

namespace memtest::limits {

// Aviso cooperativo del límite BLANDO de CPU (Arquitectura.md §9.2). El bucle
// del workload consulta cpu_limit_reached() junto con el CancellationToken y,
// si es true, termina y el worker informa `outcome=cpu_limit` (código 11).
//
//   Linux    instala un handler de SIGXCPU que solo pone una bandera
//            (volatile sig_atomic_t), como pide el diseño.
//   Windows  no hay señal equivalente: un hilo vigía lee el tiempo de CPU del
//            propio proceso (usuario + kernel) y pone la bandera al llegar a
//            `soft_seconds`. El límite duro (N + 1) lo impone el Job Object.
//
// Llamar una vez, después de ResourceLimiter::apply y antes de la carga.
// `soft_seconds` solo lo usa Windows; Linux lo ignora (lo decide el kernel).
// Devuelve false si no se pudo instalar.
bool install_cpu_limit_notifier(int soft_seconds);

// Seguro desde cualquier hilo y desde un handler de señal.
bool cpu_limit_reached() noexcept;

}  // namespace memtest::limits

#endif  // MEMTEST_LIMITS_CPULIMITNOTIFIER_HPP

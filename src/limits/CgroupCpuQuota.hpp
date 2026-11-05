#ifndef MEMTEST_LIMITS_CGROUPCPUQUOTA_HPP
#define MEMTEST_LIMITS_CGROUPCPUQUOTA_HPP

#include <cstdint>
#include <string>

namespace memtest::limits {

// Cuota de CPU con cgroup v2 (`cpu.max`). Crea un cgroup hijo bajo un ancestro
// delegado, mueve el proceso actual y lo devuelve a su cgroup original al
// limpiar. No usa privilegios especiales: depende de que el usuario pueda
// escribir `cgroup.subtree_control` y `cpu.max` en algun ancestro.
class CgroupCpuQuota {
public:
    CgroupCpuQuota() = default;
    ~CgroupCpuQuota();

    CgroupCpuQuota(const CgroupCpuQuota&) = delete;
    CgroupCpuQuota& operator=(const CgroupCpuQuota&) = delete;

    // Comprueba si el entorno permite crear un cgroup con cpu.max. No mueve
    // procesos: crea un cgroup de prueba y lo elimina.
    static bool supported(std::string* detail = nullptr);

    // Aplica `percent` (1..100) al proceso actual. Devuelve false si no se
    // pudo crear el cgroup o entrar en el.
    bool apply(int percent, std::string* detail = nullptr);

    // Lee `cpu.stat` del cgroup aplicado.
    bool read_stats(std::uint64_t& nr_throttled,
                    std::uint64_t& throttled_usec) const;

    // Vuelve al cgroup original y elimina el hijo. Es idempotente.
    void cleanup();

    bool applied() const { return applied_; }

private:
    std::string original_path_;
    std::string child_path_;
    bool applied_ = false;
};

}  // namespace memtest::limits

#endif  // MEMTEST_LIMITS_CGROUPCPUQUOTA_HPP

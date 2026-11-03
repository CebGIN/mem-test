#ifndef MEMTEST_RUNTIME_RANGE_HPP
#define MEMTEST_RUNTIME_RANGE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace memtest {

// Rango semiabierto [begin, end) de unidades de trabajo. Se usa para repartir
// un total entre tareas sin solapamientos ni huecos (Arquitectura.md §12).
struct WorkRange {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;

    std::uint64_t size() const { return end > begin ? end - begin : 0; }
    bool empty() const { return begin >= end; }
};

// Divide `total` en `parts` bloques contiguos. Los primeros `total % parts`
// bloques reciben una unidad adicional, de modo que la union de todos los
// rangos cubre exactamente [0, total). Si `index` esta fuera de rango devuelve
// un rango vacio.
inline WorkRange split_range(std::uint64_t total, std::size_t parts,
                             std::size_t index) {
    if (parts == 0 || index >= parts) {
        return {};
    }

    const std::uint64_t part_count = static_cast<std::uint64_t>(parts);
    const std::uint64_t base = total / part_count;
    const std::uint64_t remainder = total % part_count;
    const std::uint64_t index64 = static_cast<std::uint64_t>(index);

    const std::uint64_t begin =
        index64 * base + std::min<std::uint64_t>(index64, remainder);
    const std::uint64_t count =
        base + (index64 < remainder ? 1ull : 0ull);
    return {begin, begin + count};
}

}  // namespace memtest

#endif  // MEMTEST_RUNTIME_RANGE_HPP

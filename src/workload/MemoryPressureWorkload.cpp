#include "workload/MemoryPressureWorkload.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>

#include "workload/MemoryRegion.hpp"
#include "workload/WorkloadSupport.hpp"

namespace memtest {
namespace {

constexpr std::uint64_t kDefaultPageStride = 4096;
// Checkpoint de cancelacion, en paginas tocadas (Arquitectura.md §11.2).
constexpr std::uint64_t kCheckpointPages = 64;

}  // namespace

WorkloadOutcome MemoryPressureWorkload::run(const WorkloadContext& context) {
    WorkloadOutcome result;
    if (context.config == nullptr) {
        result.outcome = RunOutcome::Error;
        result.detail = "configuracion invalida: falta la configuracion";
        return result;
    }

    const ExperimentConfig& config = *context.config;
    if (config.memory_bytes == 0) {
        result.outcome = RunOutcome::Completed;
        result.detail = "sin memoria solicitada; no se toco ninguna pagina";
        return result;
    }

    const std::uint64_t stride = config.page_stride_bytes > 0
                                     ? config.page_stride_bytes
                                     : kDefaultPageStride;
    // El bucle toca una posición por cada tramo de `stride`; si el tamaño no
    // es múltiplo exacto, la última posición también cuenta.
    result.expected = config.memory_bytes / stride +
                      (config.memory_bytes % stride != 0 ? 1 : 0);

    MemoryRegion region(config.memory_bytes);
    if (!region.valid()) {
        result.outcome = region.error_code() == ENOMEM
                             ? RunOutcome::MemoryLimitHit
                             : RunOutcome::Error;
        result.detail = "mmap fallo (errno=" +
                        std::to_string(region.error_code()) + ")";
        return result;
    }

    volatile unsigned char* pages =
        static_cast<volatile unsigned char*>(region.data());
    ProgressReporter progress(context.progress, result.expected);

    std::uint64_t touched = 0;
    bool cancelled = false;
    for (std::uint64_t offset = 0; offset < config.memory_bytes;
         offset += stride) {
        pages[static_cast<std::size_t>(offset)] =
            static_cast<unsigned char>(offset & 0xffu);
        ++touched;

        if (touched % kCheckpointPages == 0) {
            progress.set(touched);
            if (context.cancel != nullptr && context.cancel->cancelled()) {
                cancelled = true;
                break;
            }
        }
    }
    progress.set(touched);

    result.observed = touched;
    if (cancelled) {
        result.outcome = RunOutcome::Stopped;
        result.detail = "detenida tras tocar " + std::to_string(touched) +
                        " de " + std::to_string(result.expected) + " paginas";
    } else {
        result.outcome = RunOutcome::Completed;
        result.detail = "paginas tocadas=" + std::to_string(touched) +
                        " de " + std::to_string(result.expected);
    }
    return result;
}

}  // namespace memtest

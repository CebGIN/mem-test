#include "workload/MemoryPressureWorkload.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include <sys/mman.h>

#include "metrics/ProcFs.hpp"
#include "workload/MemoryRegion.hpp"
#include "workload/WorkloadSupport.hpp"

#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif

namespace memtest {
namespace {

// Checkpoint de cancelacion, en paginas tocadas (Arquitectura.md §11.2).
constexpr std::uint64_t kCheckpointPages = 64;
// Tamano de bloque para pedir el reemplazo de paginas al kernel.
constexpr std::uint64_t kSwapChunkBytes = 1024ull * 1024ull;

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

    // El paso se fija al tamano real de pagina: asi "paginas tocadas" cuenta
    // paginas y no posiciones muestreadas.
    const std::uint64_t stride = metrics::page_size_bytes();
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

    const bool timed = workload_uses_duration(config);
    const auto deadline = timed ? workload_deadline(config)
                                : std::chrono::steady_clock::time_point{};

    std::uint64_t touched = 0;
    bool cancelled = false;
    bool deadline_reached = false;
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
            if (timed && workload_time_expired(deadline)) {
                deadline_reached = true;
                break;
            }
        }
    }
    progress.set(touched);

    // MADV_PAGEOUT pide al kernel que reemplace las paginas tocadas. Es
    // best-effort: si no hay SWAP o alguna pagina esta ocupada, se ignora el
    // fallo y la medicion muestra lo que el sistema haya hecho.
    if (config.force_swap && !cancelled && !deadline_reached) {
        unsigned char* base = static_cast<unsigned char*>(region.data());
        for (std::uint64_t offset = 0; offset < config.memory_bytes;
             offset += kSwapChunkBytes) {
            if (context.cancel != nullptr && context.cancel->cancelled()) {
                cancelled = true;
                break;
            }
            const std::uint64_t remaining = config.memory_bytes - offset;
            const std::size_t length = static_cast<std::size_t>(
                std::min(kSwapChunkBytes, remaining));
            (void)::madvise(base + offset, length, MADV_PAGEOUT);
        }
    }

    // Con duración real, mantener la región mapeada el resto del tiempo para
    // que el sistema tenga oportunidad de mostrar RAM residente y SWAP.
    if (timed && !cancelled && !deadline_reached) {
        while (!workload_time_expired(deadline)) {
            if (context.cancel != nullptr && context.cancel->cancelled()) {
                cancelled = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    result.observed = touched;
    if (cancelled) {
        result.outcome = RunOutcome::Stopped;
        result.detail = "detenida tras tocar " + std::to_string(touched) +
                        " de " + std::to_string(result.expected) + " paginas";
    } else {
        result.outcome = RunOutcome::Completed;
        result.detail = "paginas tocadas=" + std::to_string(touched) +
                        " de " + std::to_string(result.expected);
        if (deadline_reached) {
            result.detail += " (duracion alcanzada)";
        }
    }
    return result;
}

}  // namespace memtest

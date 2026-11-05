cmake_minimum_required(VERSION 3.14)

project(mem-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GuayabaTUIEngine vive en external/ como submódulo de Git. Si aun no se ha
# inicializado:
#   git submodule update --init --recursive
set(GUAYABA_ENGINE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/GuayabaTUIEngine"
    CACHE PATH "Ruta al arbol de fuentes de GuayabaTUIEngine")

if(NOT EXISTS "${GUAYABA_ENGINE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "No se encontro GuayabaTUIEngine en '${GUAYABA_ENGINE_DIR}'.\n"
        "Inicializa el submódulo con:\n"
        "  git submodule update --init --recursive")
endif()

if(WIN32)
    message(FATAL_ERROR
        "El MVP actual es Linux. El soporte Windows esta estacionado y no se "
        "compila en este arbol.")
endif()

add_subdirectory(
    "${GUAYABA_ENGINE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/GuayabaTUIEngine")

# --- Avisos de compilacion -------------------------------------------------------
# Solo para los targets del proyecto: Guayaba se agrega como subdirectorio y no
# debe heredar estas opciones.
function(memtest_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf8 /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# --- Núcleo headless -------------------------------------------------------------
# memtest_core no incluye Guayaba: dominio, core, métricas, límites, runtime,
# workloads, proceso, app y el controlador fake se prueban sin terminal.
add_library(memtest_core STATIC
    src/app/ExperimentController.cpp
    src/app/ExperimentRunner.cpp
    src/app/FakeExperimentController.cpp
    src/core/CsvExporter.cpp
    src/core/ResultStore.cpp
    src/core/SystemInfo.cpp
    src/domain/Validation.cpp
    src/limits/CgroupCpuQuota.cpp
    src/limits/CpuLimitNotifierPosix.cpp
    src/limits/PlatformLimitsPosix.cpp
    src/limits/ResourceLimiter.cpp
    src/metrics/MetricsSourceFactory.cpp
    src/metrics/ProcFs.cpp
    src/metrics/ProcMetricsSource.cpp
    src/process/PosixProcessSupervisor.cpp
    src/process/WorkerProtocol.cpp
    src/runtime/NativeThreadPool.cpp
    src/runtime/TaskPool.cpp
    src/runtime/WorkerPoolFactory.cpp
    src/workload/ConcurrencyWorkload.cpp
    src/workload/CpuLimitWorkload.cpp
    src/workload/MemoryPressureWorkload.cpp
    src/workload/MemoryRegion.cpp
    src/workload/SharedCounterWorkload.cpp
    src/workload/WorkloadFactory.cpp)
target_include_directories(memtest_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(memtest_core PUBLIC Threads::Threads)
memtest_target_warnings(memtest_core)

# --- Aplicación TUI --------------------------------------------------------------
# La capa ui/ solo depende de IExperimentController y domain/; se separa en su
# propia librería para que la prueben tanto la app como los tests headless.
add_library(memtest_ui STATIC
    src/ui/CompareView.cpp
    src/ui/ExperimentForm.cpp
    src/ui/HistoryPanel.cpp
    src/ui/MainScene.cpp
    src/ui/MetricsPanel.cpp
    src/ui/UiFormat.cpp)
target_link_libraries(memtest_ui PUBLIC
    memtest_core
    GuayabaTUIEngine::GuayabaTUIEngine)
memtest_target_warnings(memtest_ui)

add_executable(mem-test src/main.cpp)
target_link_libraries(mem-test PRIVATE memtest_ui)
memtest_target_warnings(mem-test)

# --- Proceso hijo -----------------------------------------------------------------
add_executable(mem-test-worker src/worker/main.cpp)
target_link_libraries(mem-test-worker PRIVATE memtest_core)
memtest_target_warnings(mem-test-worker)

# --- Pruebas ---------------------------------------------------------------------
enable_testing()

add_executable(memtest_tests
    tests/main.cpp
    tests/test_domain.cpp
    tests/test_experiment_controller.cpp
    tests/test_fake_controller.cpp
    tests/test_main_scene.cpp
    tests/test_process_supervisor.cpp
    tests/test_worker_pool.cpp
    tests/test_worker_protocol.cpp
    tests/test_workload_sync.cpp)
target_include_directories(memtest_tests PRIVATE tests)
target_link_libraries(memtest_tests PRIVATE memtest_core memtest_ui)
# Los tests de supervisor y controlador lanzan el worker real.
add_dependencies(memtest_tests mem-test-worker)
memtest_target_warnings(memtest_tests)

add_test(NAME domain COMMAND memtest_tests domain)
add_test(NAME fake_controller COMMAND memtest_tests fake_controller)
add_test(NAME main_scene COMMAND memtest_tests main_scene)
add_test(NAME worker_pool COMMAND memtest_tests worker_pool)
add_test(NAME workload_sync COMMAND memtest_tests workload_sync)
add_test(NAME worker_protocol COMMAND memtest_tests worker_protocol)
add_test(NAME process_supervisor COMMAND memtest_tests process_supervisor)
add_test(NAME experiment_controller COMMAND memtest_tests experiment_controller)

# Pruebas standalone de métricas y límites (WP1). Incluyen su propio main()
# porque test_limits se relanza con --child para aplicar límites al proceso.
add_subdirectory(tests)

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

add_subdirectory(
    "${GUAYABA_ENGINE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/GuayabaTUIEngine")

# --- Avisos de compilacion -------------------------------------------------------
# Solo para los targets del proyecto: Guayaba se agrega como subdirectorio y no
# debe heredar estas opciones.
function(memtest_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# --- Núcleo headless -------------------------------------------------------------
# memtest_core no incluye Guayaba: dominio, core, métricas, límites, runtime,
# workloads, proceso, app y el controlador fake se prueban sin terminal.
add_library(memtest_core STATIC
    src/core/SystemInfo.cpp
    src/domain/Validation.cpp
    src/metrics/ProcFs.cpp)
target_include_directories(memtest_core PUBLIC src)
memtest_target_warnings(memtest_core)

# --- Aplicación TUI --------------------------------------------------------------
add_executable(mem-test src/main.cpp)
target_link_libraries(mem-test PRIVATE
    memtest_core
    GuayabaTUIEngine::GuayabaTUIEngine)
memtest_target_warnings(mem-test)

# --- Pruebas ---------------------------------------------------------------------
enable_testing()

add_executable(memtest_tests
    tests/main.cpp
    tests/test_domain.cpp)
target_include_directories(memtest_tests PRIVATE tests)
target_link_libraries(memtest_tests PRIVATE memtest_core)
memtest_target_warnings(memtest_tests)

add_test(NAME domain COMMAND memtest_tests domain)

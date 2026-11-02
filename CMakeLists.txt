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

add_library(memtest_core STATIC
    src/metrics/ProcFs.cpp)
target_include_directories(memtest_core PUBLIC src)

add_executable(mem-test src/main.cpp)
target_link_libraries(mem-test PRIVATE
    memtest_core
    GuayabaTUIEngine::GuayabaTUIEngine)

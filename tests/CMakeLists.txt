# Pruebas headless: sin red, sin root, sin Guayaba. Cada una es un ejecutable
# registrado en CTest.
function(memtest_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE memtest_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE
        MEMTEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Parseo de fixtures de /proc y un hijo real (solo Linux).
if(NOT WIN32)
    memtest_add_test(test_proc_metrics)
endif()


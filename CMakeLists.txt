cmake_minimum_required(VERSION 3.15)
project(BballCore CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# ── Core library (shared across all targets) ─────────────────────────────────

set(CORE_SOURCES
    src/PlayerEntity.cpp
    src/Basketball.cpp
    src/Court.cpp
    src/SynergyEngine.cpp
    src/GameEconomy.cpp
    src/GameSeason.cpp
    src/ShotProbability.cpp
    src/GameManager.cpp
)

# Static library for native linking (tests, Swift C++ interop, pybind11)
add_library(bball_core STATIC ${CORE_SOURCES})
target_compile_options(bball_core PRIVATE -O2 -Wall -Wextra)

# ── Native test runner ────────────────────────────────────────────────────────

if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_engine.cpp")
    add_executable(test_runner tests/test_engine.cpp)
    target_link_libraries(test_runner bball_core)
endif()

# ── WebAssembly target (only when compiling with Emscripten) ──────────────────

if(EMSCRIPTEN)
    add_executable(engine ${CORE_SOURCES} bindings/wasm/Bindings.cpp)
    target_compile_options(engine PRIVATE -O3 -Wall -Wextra)
    set_target_properties(engine PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/wasm"
        OUTPUT_NAME "engine"
        SUFFIX ".js"
    )
    target_link_options(engine PRIVATE
        -lembind
        -sWASM=1
        -sMODULARIZE=1
        -sEXPORT_NAME=Module
        -sALLOW_MEMORY_GROWTH=1
    )
endif()

# ── pybind11 target (when found) ─────────────────────────────────────────────

find_package(pybind11 QUIET)
if(pybind11_FOUND AND NOT EMSCRIPTEN)
    pybind11_add_module(bball_py bindings/python/bindings.cpp)
    target_link_libraries(bball_py PRIVATE bball_core)
    set_target_properties(bball_py PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/python"
    )
endif()

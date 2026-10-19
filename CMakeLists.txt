cmake_minimum_required(VERSION 3.20)
project(EvolutionSimulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EVOSIM_BUILD_TESTS "Build the Catch2 test suite" ON)
option(EVOSIM_BUILD_CLI "Build the evosim command-line tool" ON)
option(EVOSIM_SANITIZE "Build with address/undefined sanitizers" OFF)

if(EMSCRIPTEN)
  set(EVOSIM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(EVOSIM_BUILD_CLI OFF CACHE BOOL "" FORCE)
endif()

# ---------------------------------------------------------------- engine library
add_library(evosim_core STATIC
  src/core/Json.cpp
  src/core/SimulationConfig.cpp
  src/core/SpatialGrid.cpp
  src/core/Simulation.cpp
  src/organisms/Genome.cpp
  src/organisms/Phenotype.cpp
  src/organisms/Brain.cpp
  src/ecology/ResourceField.cpp
  src/ecology/Environment.cpp
  src/analytics/LineageTracker.cpp
  src/analytics/Speciation.cpp
  src/analytics/Statistics.cpp
  src/analytics/StatsMath.cpp
  src/analytics/ExperimentRunner.cpp
  src/serialization/Checkpoint.cpp
)
target_include_directories(evosim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(evosim_core PUBLIC EVOSIM_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(evosim_core PRIVATE /W4 /permissive- /fp:precise)
else()
  target_compile_options(evosim_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
  # Bit-reproducibility across compilers/platforms: never fuse a*b+c into FMA.
  target_compile_options(evosim_core PUBLIC -ffp-contract=off)
endif()

if(EVOSIM_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
  target_compile_options(evosim_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(evosim_core PUBLIC -fsanitize=address,undefined)
endif()

if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(evosim_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------- CLI
if(EVOSIM_BUILD_CLI)
  add_executable(evosim apps/evosim_cli.cpp)
  target_link_libraries(evosim PRIVATE evosim_core)
endif()

# ---------------------------------------------------------------- tests
if(EVOSIM_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  enable_testing()
  add_executable(evosim_tests
    tests/test_json_config.cpp
    tests/test_genome.cpp
    tests/test_spatial.cpp
    tests/test_resources.cpp
    tests/test_simulation.cpp
    tests/test_properties.cpp
    tests/test_sanity.cpp
    tests/test_checkpoint.cpp
    tests/test_experiments.cpp
    tests/test_detmath.cpp
  )
  target_link_libraries(evosim_tests PRIVATE evosim_core Catch2::Catch2WithMain)
  target_compile_definitions(evosim_tests PRIVATE EVOSIM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(evosim_tests)
endif()

# ---------------------------------------------------------------- WebAssembly
if(EMSCRIPTEN)
  add_executable(evosim_wasm src/interfaces/wasm_api.cpp)
  target_link_libraries(evosim_wasm PRIVATE evosim_core)
  set_target_properties(evosim_wasm PROPERTIES OUTPUT_NAME "evosim" SUFFIX ".mjs")
  target_compile_options(evosim_core PRIVATE -fexceptions)
  target_compile_options(evosim_wasm PRIVATE -fexceptions)
  target_link_options(evosim_wasm PRIVATE
    -fexceptions
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createEvoModule
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB
    -sTOTAL_STACK=1MB
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAPF32','HEAPU32']"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
  )
endif()

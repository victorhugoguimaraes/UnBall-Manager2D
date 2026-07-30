cmake_minimum_required(VERSION 3.20)
project(UnBallManager2D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets 3.1 REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
find_package(nlohmann_json 3.11 REQUIRED)
option(UNBALL_ENABLE_PROFILING "Gera dados para analise com gprof" OFF)

add_executable(UnBall-Manager2D
    src/main.cpp
    src/MainFrame.cpp
    src/TeamStore.cpp
)

target_include_directories(UnBall-Manager2D PRIVATE src)
target_link_libraries(UnBall-Manager2D PRIVATE ${wxWidgets_LIBRARIES} nlohmann_json::nlohmann_json)

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

enable_project_warnings(UnBall-Manager2D)

if(UNBALL_ENABLE_PROFILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # So habilite quando for medir: -pg adiciona instrumentacao ao executavel.
    target_compile_options(UnBall-Manager2D PRIVATE -pg)
    target_link_options(UnBall-Manager2D PRIVATE -pg)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(TeamStoreTests tests/TeamStoreTests.cpp src/TeamStore.cpp)
    target_include_directories(TeamStoreTests PRIVATE src)
    target_link_libraries(TeamStoreTests PRIVATE nlohmann_json::nlohmann_json)
    enable_project_warnings(TeamStoreTests)
    add_test(NAME TeamStoreTests COMMAND TeamStoreTests)
endif()

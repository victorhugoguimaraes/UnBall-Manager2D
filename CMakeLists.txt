cmake_minimum_required(VERSION 3.20)
project(UnBallManager2D VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Use Linux ou WSL: o gerenciador depende de grupos de processos POSIX.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
find_package(nlohmann_json 3.11 REQUIRED)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

add_custom_target(copy-extractor ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/python/unball_extract
        ${CMAKE_CURRENT_BINARY_DIR}/extractor/unball_extract
    VERBATIM
)
add_custom_target(copy-updater ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/updater
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/python/unball_update.py ${CMAKE_CURRENT_BINARY_DIR}/updater/unball_update.py
    VERBATIM
)
set(UNBALL_SOURCE_REVISION "" CACHE STRING "Source commit used by a downloaded release")
set(UNBALL_BUILD_REVISION "${UNBALL_SOURCE_REVISION}")
if(NOT UNBALL_BUILD_REVISION AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(Git_FOUND)
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.git")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD" "${CMAKE_CURRENT_SOURCE_DIR}/.git/logs/HEAD")
        endif()
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE UNBALL_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
endif()
if(NOT UNBALL_BUILD_REVISION)
    set(UNBALL_BUILD_REVISION "unknown")
endif()
if(NOT UNBALL_BUILD_REVISION STREQUAL "unknown" AND NOT UNBALL_BUILD_REVISION MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Invalid source revision")
endif()
configure_file(src/AppVersion.hpp.in generated/AppVersion.hpp @ONLY)
option(UNBALL_ENABLE_PROFILING "Gera dados para analise com gprof" OFF)

add_library(manager-core
    src/TeamStore.cpp
    src/FileIO.cpp
    src/Session.cpp
    src/ChildProcess.cpp
    src/DataExport.cpp
    src/BatchRun.cpp
    src/AppUpdate.cpp
)
target_include_directories(manager-core PUBLIC src PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(manager-core PUBLIC nlohmann_json::nlohmann_json)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/unball.png")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/unball.png" UNBALL_LOGO_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," UNBALL_LOGO_DATA "${UNBALL_LOGO_HEX}")
configure_file(src/BrandingData.hpp.in generated/BrandingData.hpp @ONLY)
add_library(manager-branding src/Branding.cpp)
target_include_directories(manager-branding PUBLIC src PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(manager-branding PUBLIC ${wxWidgets_LIBRARIES})

add_executable(UnBall-Manager2D src/main.cpp src/MainFrame.cpp)

add_dependencies(UnBall-Manager2D copy-extractor copy-updater)
install(TARGETS UnBall-Manager2D RUNTIME DESTINATION bin)
install(FILES assets/unball.png DESTINATION share/icons/hicolor/512x512/apps RENAME unball-manager2d.png)
install(FILES assets/unball-manager2d.desktop DESTINATION share/applications)
install(FILES assets/COPYING-UNBALL DESTINATION share/unball-manager)
install(FILES python/unball_update.py DESTINATION share/unball-manager/updater)
install(DIRECTORY python/unball_extract DESTINATION share/unball-manager/extractor
    PATTERN "__pycache__" EXCLUDE)

target_include_directories(UnBall-Manager2D PRIVATE src)
target_link_libraries(UnBall-Manager2D PRIVATE manager-core manager-branding ${wxWidgets_LIBRARIES})

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

enable_project_warnings(UnBall-Manager2D)
enable_project_warnings(manager-core)
enable_project_warnings(manager-branding)

if(UNBALL_ENABLE_PROFILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # So habilite quando for medir: -pg adiciona instrumentacao ao executavel.
    target_compile_options(UnBall-Manager2D PRIVATE -pg)
    target_link_options(UnBall-Manager2D PRIVATE -pg)
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(test TeamStoreTests SessionTests ChildProcessTests DataExportTests BatchRunTests AppUpdateTests)
        add_executable(${test} tests/${test}.cpp)
        target_link_libraries(${test} PRIVATE manager-core)
        enable_project_warnings(${test})
        add_test(NAME ${test} COMMAND ${test})
        set_tests_properties(${test} PROPERTIES TIMEOUT 25)
    endforeach()
    target_compile_definitions(DataExportTests PRIVATE UNBALL_TEST_PYTHON="${Python3_EXECUTABLE}" UNBALL_TEST_EXTRACTOR="${CMAKE_CURRENT_SOURCE_DIR}/python")
    target_compile_definitions(AppUpdateTests PRIVATE UNBALL_TEST_PYTHON="${Python3_EXECUTABLE}")
    add_test(NAME ExtractorTests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests/python -v)
    set_tests_properties(ExtractorTests PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python;PYTHONDONTWRITEBYTECODE=1" TIMEOUT 30)
    add_executable(SessionSmokeTest tests/SessionSmokeTest.cpp)
    add_dependencies(SessionSmokeTest copy-extractor)
    target_link_libraries(SessionSmokeTest PRIVATE manager-core)
    enable_project_warnings(SessionSmokeTest)
    option(UNBALL_ENABLE_GUI_TESTS "Compila teste wxWidgets para executar em um display isolado" OFF)
    if(UNBALL_ENABLE_GUI_TESTS)
        add_executable(ManagerUiTests tests/ManagerUiTests.cpp src/MainFrame.cpp)
        add_dependencies(ManagerUiTests copy-extractor copy-updater)
        target_link_libraries(ManagerUiTests PRIVATE manager-core manager-branding ${wxWidgets_LIBRARIES})
        enable_project_warnings(ManagerUiTests)
    endif()
endif()

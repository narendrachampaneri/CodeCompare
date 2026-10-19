cmake_minimum_required(VERSION 3.21)

project(CodeCompare
    VERSION 0.1.1
    DESCRIPTION "Folder and line-by-line code comparison tool"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CC_BUILD_GUI   "Build the Qt desktop application" ON)
option(CC_BUILD_CLI   "Build the command-line tool"      ON)
option(CC_BUILD_TESTS "Build the unit tests"             ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
else()
    add_compile_options(-Wall -Wextra)
endif()

# MinGW: link statically against the GCC/C++ runtime so the .exe does not
# depend on libgcc/libstdc++ DLLs being next to it.
if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

# ---------------- core engine (no Qt) ----------------
add_library(cc_core STATIC
    src/core/Types.h
    src/core/IgnoreRules.h   src/core/IgnoreRules.cpp
    src/core/TextDecode.h    src/core/TextDecode.cpp
    src/core/Diff.h          src/core/Diff.cpp
    src/core/FolderCompare.h src/core/FolderCompare.cpp
    src/core/Report.h        src/core/Report.cpp
)
target_include_directories(cc_core PUBLIC src/core)
# GCC 8 needs stdc++fs for <filesystem>
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(cc_core PUBLIC stdc++fs)
endif()

# ---------------- command-line tool ----------------
if(CC_BUILD_CLI)
    add_executable(codecompare-cli src/cli/main.cpp)
    target_link_libraries(codecompare-cli PRIVATE cc_core)
    if(WIN32)
        target_link_libraries(codecompare-cli PRIVATE shell32)
    endif()
endif()

# ---------------- tests ----------------
if(CC_BUILD_TESTS)
    enable_testing()
    add_executable(cc_tests tests/test_core.cpp)
    target_link_libraries(cc_tests PRIVATE cc_core)
    add_test(NAME core_tests COMMAND cc_tests)
endif()

# ---------------- Qt desktop application ----------------
if(CC_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent)
    qt_standard_project_setup()

    set(GUI_SOURCES
        src/gui/main.cpp
        src/gui/MainWindow.h       src/gui/MainWindow.cpp
        src/gui/FolderTreeModel.h  src/gui/FolderTreeModel.cpp
        src/gui/DiffView.h         src/gui/DiffView.cpp
        src/gui/DiffPane.h         src/gui/DiffPane.cpp
        src/gui/OverviewBar.h      src/gui/OverviewBar.cpp
        src/gui/Theme.h            src/gui/Theme.cpp
        src/gui/PathUtil.h
        resources/resources.qrc
    )
    if(WIN32)
        configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
        list(APPEND GUI_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    endif()

    qt_add_executable(CodeCompare WIN32 ${GUI_SOURCES})
    target_compile_definitions(CodeCompare PRIVATE
        CC_VERSION="${PROJECT_VERSION}")
    target_link_libraries(CodeCompare PRIVATE cc_core Qt6::Widgets Qt6::Concurrent)

    # Developer-only screenshot tool (not shipped).
    option(CC_BUILD_SNAPSHOT "Build the GUI screenshot tool" OFF)
    if(CC_BUILD_SNAPSHOT)
        set(SNAP_SOURCES ${GUI_SOURCES})
        list(REMOVE_ITEM SNAP_SOURCES src/gui/main.cpp ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
        qt_add_executable(cc_gui_snapshot tests/gui_snapshot.cpp ${SNAP_SOURCES})
        target_include_directories(cc_gui_snapshot PRIVATE src/gui)
        target_compile_definitions(cc_gui_snapshot PRIVATE CC_VERSION="${PROJECT_VERSION}")
        target_link_libraries(cc_gui_snapshot PRIVATE cc_core Qt6::Widgets Qt6::Concurrent)
    endif()

    install(TARGETS CodeCompare RUNTIME DESTINATION .)
endif()

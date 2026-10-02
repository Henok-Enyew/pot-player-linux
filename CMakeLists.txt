cmake_minimum_required(VERSION 3.16)

project(PotPlayerLinux
    VERSION 0.1.0
    DESCRIPTION "A lightweight Qt6 + libmpv media player for Linux"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_executable(pot-player
    resources/skin.qrc
    src/main.cpp
    src/ControlBar.cpp
    src/ControlBar.h
    src/Icons.cpp
    src/Icons.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/MpvHelpers.h
    src/MpvWidget.cpp
    src/MpvWidget.h
    src/OsdWidget.cpp
    src/OsdWidget.h
    src/PlayerMenu.cpp
    src/PlayerMenu.h
    src/PlaylistDrawer.cpp
    src/PlaylistDrawer.h
    src/SeekBar.cpp
    src/SeekBar.h
    src/Theme.cpp
    src/Theme.h
    src/ThumbnailGenerator.cpp
    src/ThumbnailGenerator.h
    src/ThumbnailPopup.cpp
    src/ThumbnailPopup.h
    src/TimeFormat.h
    src/TitleBar.cpp
    src/TitleBar.h
)

target_compile_definitions(pot-player PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pot-player PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(pot-player PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    PkgConfig::MPV
)

install(TARGETS pot-player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

cmake_minimum_required(VERSION 3.16)

project(PotPlayerLinux
    VERSION 0.2.0
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

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network OpenGL OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

# OpenSubtitles.com needs an API key. One built in here is used unless the
# user enters their own in the subtitle download settings. Release builds
# take it from $OPENSUBTITLES_API_KEY or an untracked opensubtitles-api-key.txt.
set(_opensubtitles_key "$ENV{OPENSUBTITLES_API_KEY}")
if(_opensubtitles_key STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/opensubtitles-api-key.txt")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/opensubtitles-api-key.txt" _opensubtitles_key LIMIT_COUNT 1)
endif()
set(POTPLAYER_OPENSUBTITLES_API_KEY "${_opensubtitles_key}" CACHE STRING "Built-in OpenSubtitles.com API key")

option(POTPLAYER_BUILD_TESTS "Build the integration tests (need an X or Wayland display)" OFF)

# Everything but main(), so the tests can drive the real window.
add_library(pot-player-core OBJECT
    resources/skin.qrc
    src/ArtworkExtractor.cpp
    src/ArtworkExtractor.h
    src/AudioArtwork.cpp
    src/AudioArtwork.h
    src/AudioControlDialog.cpp
    src/AudioControlDialog.h
    src/AudioController.cpp
    src/AudioController.h
    src/AudioEffects.cpp
    src/AudioEffects.h
    src/AudioView.cpp
    src/AudioView.h
    src/ControlBar.cpp
    src/ControlBar.h
    src/EmptyStateWidget.cpp
    src/EmptyStateWidget.h
    src/Icons.cpp
    src/Icons.h
    src/LibraryPanel.cpp
    src/LibraryPanel.h
    src/LiveStreamDialog.cpp
    src/LiveStreamDialog.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/MediaCutter.cpp
    src/MediaCutter.h
    src/MediaCutterDialog.cpp
    src/MediaCutterDialog.h
    src/MediaDownloader.cpp
    src/MediaDownloader.h
    src/MediaDownloaderDialog.cpp
    src/MediaDownloaderDialog.h
    src/MediaFiles.cpp
    src/MediaFiles.h
    src/MediaLibrary.cpp
    src/MediaLibrary.h
    src/MediaProber.cpp
    src/MediaProber.h
    src/MpvHelpers.h
    src/MpvWidget.cpp
    src/MpvWidget.h
    src/OpenSubtitlesClient.cpp
    src/OpenSubtitlesClient.h
    src/OsdWidget.cpp
    src/OsdWidget.h
    src/PlayerMenu.cpp
    src/PlayerMenu.h
    src/PlaylistController.cpp
    src/PlaylistController.h
    src/PlaylistDrawer.cpp
    src/PlaylistDrawer.h
    src/PlaylistOps.cpp
    src/PlaylistOps.h
    src/PlaylistSession.cpp
    src/PlaylistSession.h
    src/SeekBar.cpp
    src/SeekBar.h
    src/StreamCatalog.cpp
    src/StreamCatalog.h
    src/SubtitleDownloadDialog.cpp
    src/SubtitleDownloadDialog.h
    src/SubtitleSearch.cpp
    src/SubtitleSearch.h
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

target_include_directories(pot-player-core PUBLIC src)

target_compile_definitions(pot-player-core PUBLIC
    APP_VERSION="${PROJECT_VERSION}"
    POTPLAYER_OPENSUBTITLES_API_KEY="${POTPLAYER_OPENSUBTITLES_API_KEY}"
    QT_NO_KEYWORDS
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pot-player-core PUBLIC -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(pot-player-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    PkgConfig::MPV
)

add_executable(pot-player src/main.cpp)
target_link_libraries(pot-player PRIVATE pot-player-core)

if(POTPLAYER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS pot-player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/linux/org.github.potlinux.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/linux/org.github.potlinux.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES packaging/linux/org.github.potlinux.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/pot-player)

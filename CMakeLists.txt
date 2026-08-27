cmake_minimum_required(VERSION 3.16)

# Must be set BEFORE project() so every compiler and linker invocation sees it.
#
# 14.2, not 14.0 like the sibling projects: CoreAudio process taps
# (AudioHardwareCreateProcessTap / CATapDescription) landed in 14.2, and they
# are the entire reason this app can capture system audio without shipping a
# virtual audio driver.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "Minimum macOS version")

project(hyperwave VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

option(HYPERWAVE_BUILD_TESTS "Build the headless test suite" ON)

# --- versions ---------------------------------------------------------------
#
# Two numbers, same scheme as hyperbin and hypershot:
#   PROJECT_VERSION   marketing version, what people read -> CFBundleShortVersionString
#   APP_VERSION_BUILD monotonic commit count              -> CFBundleVersion
#
# The build number has to be monotonic because updaters compare it numerically,
# and two builds both calling themselves "0.1.0" are indistinguishable.
find_package(Git QUIET)
if(NOT APP_VERSION_BUILD AND GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE APP_VERSION_BUILD
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT APP_VERSION_BUILD)
    set(APP_VERSION_BUILD "0")
endif()
if(NOT APP_VERSION_DISPLAY)
    # "0.1.0" on a working copy nine commits past the tag is a lie that only
    # ever surfaces in a bug report.
    set(APP_VERSION_DISPLAY "${PROJECT_VERSION}-dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- locating Qt ------------------------------------------------------------
#
# Lifted from hyperbin. The empty-string check is load-bearing: an editor that
# expands an unset $env{QT_ROOT} hands us "" rather than nothing, and a
# defined-but-empty CMAKE_PREFIX_PATH silently defeats the search below.
if(NOT CMAKE_PREFIX_PATH)
    if(DEFINED ENV{QT_ROOT})
        set(CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
    else()
        if(APPLE)
            file(GLOB _qt_dirs "$ENV{HOME}/Qt/6.*/macos")
        elseif(WIN32)
            if(MSVC)
                set(_qt_abi "msvc*")
            else()
                set(_qt_abi "*mingw*")
            endif()
            file(GLOB _qt_dirs "C:/Qt/6.*/${_qt_abi}" "$ENV{USERPROFILE}/Qt/6.*/${_qt_abi}")
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]64")
                list(FILTER _qt_dirs INCLUDE REGEX "arm64$")
            else()
                list(FILTER _qt_dirs EXCLUDE REGEX "arm64$")
            endif()
        else()
            file(GLOB _qt_dirs "$ENV{HOME}/Qt/6.*/gcc_64")
        endif()
        if(_qt_dirs)
            list(SORT _qt_dirs COMPARE NATURAL ORDER DESCENDING)
            list(GET _qt_dirs 0 _qt_found)
            set(CMAKE_PREFIX_PATH "${_qt_found}")
            message(STATUS "Qt not specified; auto-detected ${_qt_found}")
        endif()
    endif()
endif()

set(HYPERWAVE_QT_COMPONENTS Core Gui GuiPrivate Widgets Svg Qml Quick QuickControls2 ShaderTools Test)

find_package(Qt6 6.11 QUIET COMPONENTS ${HYPERWAVE_QT_COMPONENTS})
if(NOT Qt6_FOUND)
    if(WIN32)
        message(FATAL_ERROR
            "Qt 6.11+ not found.\n"
            "Point the build at it in any one of these ways:\n"
            "  set QT_ROOT=C:\\Qt\\6.11.2\\msvc2022_64\n"
            "  cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64\n")
    else()
        message(FATAL_ERROR
            "Qt 6.11+ not found.\n"
            "Point the build at it in any one of these ways:\n"
            "  export QT_ROOT=~/Qt/6.11.2/macos\n"
            "  cmake --preset dev -DCMAKE_PREFIX_PATH=~/Qt/6.11.2/macos\n"
            "  create CMakeUserPresets.json with a preset inheriting 'dev'\n")
    endif()
endif()
find_package(Qt6 6.11 REQUIRED COMPONENTS ${HYPERWAVE_QT_COMPONENTS})
qt_standard_project_setup(REQUIRES 6.11)

# --- the shared core --------------------------------------------------------
#
# dsp/, audio/ and render/ are deliberately a separate library with no platform
# dependencies at all. The rule is enforced by what this target links: Qt Core
# and Gui, nothing else. If a #ifdef Q_OS_* ever appears in here the
# cross-platform story has already broken -- the Windows tray icon and the
# macOS status item are meant to be showing pixels from the SAME renderer, and
# the moment the two platforms run different DSP you get to chase "the bars
# look different on Windows" for a day.
add_library(hyperwave_core STATIC
    src/dsp/Fft.cpp
    src/dsp/Fft.h
    src/dsp/Window.cpp
    src/dsp/Window.h
    src/dsp/BandMapper.cpp
    src/dsp/BandMapper.h
    src/dsp/Ballistics.cpp
    src/dsp/Ballistics.h
    src/dsp/SpectrumAnalyzer.cpp
    src/dsp/SpectrumAnalyzer.h
    src/dsp/DspConfig.h
    src/dsp/FrameState.h
    src/dsp/SeqLock.h
    src/audio/AudioRing.h
    src/audio/AudioSource.h
    src/audio/Downmix.cpp
    src/audio/Downmix.h
    src/render/BarRenderer.cpp
    src/render/StrandRenderer.cpp
    src/render/LineRenderer.cpp
    src/render/MirrorRenderer.cpp
    src/render/RingRenderer.cpp
    src/render/GpuContext.cpp
    src/render/GpuPost.cpp
    src/render/RidgeRenderer.cpp
    src/render/BarRenderer.h
    src/render/RenderStyle.h
    src/render/Palette.cpp
    src/render/Palette.h
)
target_include_directories(hyperwave_core PUBLIC src)
target_link_libraries(hyperwave_core PUBLIC Qt6::Core Qt6::Gui Qt6::GuiPrivate)
qt_add_shaders(hyperwave_core "hyperwave_strand_shaders"
    PREFIX "/shaders"
    BASE "src/render/shaders"
    FILES
        src/render/shaders/strands.vert
        src/render/shaders/strands.frag
        src/render/shaders/fullscreen.vert
        src/render/shaders/bright.frag
        src/render/shaders/blur.frag
        src/render/shaders/composite.frag
        src/render/shaders/rays.frag)

# --- the app ----------------------------------------------------------------
qt_add_executable(hyperwave
    src/main.cpp
    src/app/App.cpp
    src/app/App.h
    src/app/AudioController.cpp
    src/app/AudioController.h
    src/app/DspWorker.cpp
    src/app/DspWorker.h
    src/app/Settings.cpp
    src/app/Settings.h
    src/app/DebugHud.cpp
    src/app/VisualiserControls.cpp
    src/app/DebugHud.h
    src/app/Shell.h
    src/app/NowPlaying.h
    $<$<PLATFORM_ID:Darwin>:src/app/NowPlayingMac.mm>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:src/app/NowPlayingStub.cpp>
    $<$<PLATFORM_ID:Darwin>:src/app/ShellMac.mm>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:src/app/ShellStub.cpp>
    src/platform/BarPresenter.h
    # Per-platform files are selected here rather than in if() blocks so the
    # source list stays readable as one thing.
    $<$<PLATFORM_ID:Darwin>:src/platform/CoreAudioTapSource.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/CoreAudioTapSource.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/AudioCapturePermission.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/AudioCapturePermission.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/AudioLevelMeter.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/AudioLevelMeter.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/AudioProcessProbe.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/AudioProcessProbe.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacBarPresenter.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacBarPresenter.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacMenuBuilder.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacMenuBuilder.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacNotchPresenter.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacNotchPresenter.mm>
    $<$<PLATFORM_ID:Windows>:src/platform/WasapiSource.cpp>
    $<$<PLATFORM_ID:Windows>:src/platform/WinBarPresenter.cpp>
    $<$<NOT:$<OR:$<PLATFORM_ID:Darwin>,$<PLATFORM_ID:Windows>>>:src/platform/PulseSource.cpp>
    $<$<NOT:$<OR:$<PLATFORM_ID:Darwin>,$<PLATFORM_ID:Windows>>>:src/platform/LinuxBarPresenter.cpp>
)

target_include_directories(hyperwave PRIVATE src)
target_link_libraries(hyperwave PRIVATE
    hyperwave_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Qml Qt6::Quick Qt6::QuickControls2)

target_compile_definitions(hyperwave PRIVATE
    HYPERWAVE_VERSION="${APP_VERSION_DISPLAY}"
    HYPERWAVE_VERSION_BUILD="${APP_VERSION_BUILD}")

qt_add_qml_module(hyperwave
    URI hyperwave
    VERSION 1.0
    QML_FILES
        qml/Settings.qml
)

if(APPLE)
    set_target_properties(hyperwave PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.hypernuclear.hyperwave
        MACOSX_BUNDLE_BUNDLE_NAME hyperwave
        MACOSX_BUNDLE_BUNDLE_VERSION ${APP_VERSION_BUILD}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist.in
    )

    target_link_libraries(hyperwave PRIVATE
        "-framework AppKit"
        "-framework CoreAudio"       # the process tap and the aggregate device
        "-framework AudioToolbox"
        "-framework CoreGraphics"    # QImage -> CGImage for the status item layer
        "-framework QuartzCore"      # CADisplayLink, CATransaction
        "-framework ServiceManagement")

    # ARC across the Objective-C++ layer. The CoreAudio objects are C handles
    # that we release by hand regardless, so manual retain/release would buy
    # nothing here.
    set_source_files_properties(
        src/platform/CoreAudioTapSource.mm
        src/platform/AudioCapturePermission.mm
        src/platform/AudioProcessProbe.mm
        src/platform/AudioLevelMeter.mm
        src/platform/MacBarPresenter.mm
        src/platform/MacNotchPresenter.mm
        src/platform/MacMenuBuilder.mm
        src/app/ShellMac.mm
        src/app/NowPlayingMac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")

    # Sign every build with a STABLE identity.
    #
    # This is not about distribution, it is about TCC. An ad-hoc signature is
    # keyed by the code hash, so every rebuild looks like a brand new app and
    # re-prompts for "System Audio Recording" forever. Signing with the real
    # Developer ID means the grant survives rebuilds, which matters more here
    # than in the sibling projects because the audio permission IS the app.
    set(HYPERWAVE_CODESIGN_IDENTITY
        "Developer ID Application: Hypernuclear LLC (67J6LGVJDP)"
        CACHE STRING "codesign identity; empty to skip signing")
    if(HYPERWAVE_CODESIGN_IDENTITY)
        add_custom_command(TARGET hyperwave POST_BUILD
            COMMAND codesign --force --sign ${HYPERWAVE_CODESIGN_IDENTITY}
                    --timestamp=none
                    --entitlements ${CMAKE_SOURCE_DIR}/hyperwave.entitlements
                    $<TARGET_BUNDLE_DIR:hyperwave>
            COMMENT "Signing so the system-audio TCC grant survives rebuilds"
            VERBATIM)
    endif()
endif()

if(WIN32)
    set_target_properties(hyperwave PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(HYPERWAVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

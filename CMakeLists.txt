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
# Two numbers:
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
# The empty-string check is load-bearing: an editor that
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
    $<$<PLATFORM_ID:Darwin>:src/app/ShellMac.mm>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:src/app/ShellStub.cpp>
    src/platform/BarPresenter.h
    src/platform/PanelPresenter.h
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
    $<$<PLATFORM_ID:Darwin>:src/platform/InputCapture.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/InputCapture.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacBarPresenter.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacBarPresenter.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacMenuBuilder.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacMenuBuilder.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacNotchPresenter.h>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacNotchPresenter.mm>
    $<$<PLATFORM_ID:Windows>:src/platform/WasapiSource.cpp>
    $<$<PLATFORM_ID:Windows>:src/platform/WinBarPresenter.cpp>
    $<$<PLATFORM_ID:Windows>:packaging/windows/hyperwave.rc>
    $<$<NOT:$<OR:$<PLATFORM_ID:Darwin>,$<PLATFORM_ID:Windows>>>:src/platform/PulseSource.cpp>
    $<$<NOT:$<OR:$<PLATFORM_ID:Darwin>,$<PLATFORM_ID:Windows>>>:src/platform/LinuxBarPresenter.cpp>
)

# --- auto-update -----------------------------------------------------------
#
# Sparkle on macOS, WinSparkle on Windows, nothing anywhere else. Both read the
# same appcast and verify the same EdDSA signature, so one feed generator in the
# release workflow serves both.
option(HYPERWAVE_AUTO_UPDATE "Include the auto-updater (Sparkle / WinSparkle)" ON)
set(SPARKLE_VERSION "2.9.0" CACHE STRING "Sparkle release to fetch (macOS)")
set(WINSPARKLE_VERSION "0.9.1" CACHE STRING "WinSparkle release to fetch (Windows)")

# The PUBLIC half of the update signing key. It ships inside every binary
# anyway, so hiding it in a secret buys nothing and costs reviewability -- as a
# secret it becomes an invisible input that can silently disagree with the
# private half. In the tree it is diffable.
# The public half of hyperwave's update signing key. Public on purpose: it
# ships inside every binary anyway, so hiding it in a secret would buy nothing
# and cost reviewability. The private half is the SPARKLE_EDDSA_PRIVATE_KEY
# repository secret and exists nowhere in this tree.
set(HYPERWAVE_EDDSA_PUBKEY "LuJvrTpbkny/NaRDij8N77KA8aCUjCJkKK5M/dnElCA="
    CACHE STRING "ed25519 public key for appcast verification")

set(HYPERWAVE_APPCAST_URL "https://hyperwave-builds.hypernuclear.com/latest/mac/appcast.xml"
    CACHE STRING "Sparkle appcast feed URL (macOS)")
set(HYPERWAVE_APPCAST_URL_WIN "https://hyperwave-builds.hypernuclear.com/latest/win/appcast.xml"
    CACHE STRING "WinSparkle appcast feed URL")

# No key, no updater. Deliberately not an error: a contributor cloning this
# should get a working build without being handed a signing key, and a local
# build has nothing to update to anyway. It IS an error on a tag -- the release
# workflow passes the key, and refuses to publish a feed whose enclosures came
# out unsigned.
if(HYPERWAVE_AUTO_UPDATE AND NOT HYPERWAVE_EDDSA_PUBKEY)
    message(STATUS
        "No HYPERWAVE_EDDSA_PUBKEY, so the auto-updater is off for this build. "
        "An updater that cannot verify a signature is worse than no updater. "
        "See RELEASING.md for how to generate the pair.")
    set(HYPERWAVE_AUTO_UPDATE OFF)
endif()

if(HYPERWAVE_AUTO_UPDATE)
    string(LENGTH "${HYPERWAVE_EDDSA_PUBKEY}" _hw_key_len)
    if(NOT _hw_key_len EQUAL 44)
        message(FATAL_ERROR
            "HYPERWAVE_EDDSA_PUBKEY is ${_hw_key_len} characters; a base64 "
            "ed25519 public key is 44. This is checked because a truncated key "
            "fails at update time on a user's machine, not here.")
    endif()

    include(FetchContent)
    if(APPLE)
        FetchContent_Declare(sparkle
            URL "https://github.com/sparkle-project/Sparkle/releases/download/${SPARKLE_VERSION}/Sparkle-${SPARKLE_VERSION}.tar.xz")
        FetchContent_MakeAvailable(sparkle)
        set(SPARKLE_FRAMEWORK_DIR "${sparkle_SOURCE_DIR}")
        set(HYPERWAVE_UPDATE_SOURCES src/update/SparkleUpdater.h src/update/SparkleUpdater.mm)
    elseif(WIN32)
        FetchContent_Declare(winsparkle
            URL "https://github.com/vslavik/winsparkle/releases/download/v${WINSPARKLE_VERSION}/WinSparkle-${WINSPARKLE_VERSION}.zip")
        FetchContent_MakeAvailable(winsparkle)
        set(WINSPARKLE_DIR "${winsparkle_SOURCE_DIR}")
        set(HYPERWAVE_UPDATE_SOURCES src/update/WinSparkleUpdater.h src/update/WinSparkleUpdater.cpp)
    else()
        set(HYPERWAVE_UPDATE_SOURCES src/update/NoUpdater.cpp)
    endif()
else()
    set(HYPERWAVE_UPDATE_SOURCES src/update/NoUpdater.cpp)
endif()

target_sources(hyperwave PRIVATE src/update/AppUpdater.h ${HYPERWAVE_UPDATE_SOURCES})

target_include_directories(hyperwave PRIVATE src)
target_link_libraries(hyperwave PRIVATE
    hyperwave_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Qml Qt6::Quick Qt6::QuickControls2)

target_compile_definitions(hyperwave PRIVATE
    HYPERWAVE_VERSION="${APP_VERSION_DISPLAY}"
    HYPERWAVE_VERSION_BUILD="${APP_VERSION_BUILD}")

if(HYPERWAVE_AUTO_UPDATE)
    target_compile_definitions(hyperwave PRIVATE HYPERWAVE_AUTO_UPDATE=1)
    if(APPLE)
        # The framework is embedded and signed by scripts/package-macos.sh.
        # @loader_path/../Frameworks is where it ends up inside the bundle.
        # -F has to reach the COMPILER as well as the linker. Only linking
        target_compile_options(hyperwave PRIVATE "-F${SPARKLE_FRAMEWORK_DIR}")
        target_link_libraries(hyperwave PRIVATE "-F${SPARKLE_FRAMEWORK_DIR}" "-framework Sparkle")
        target_link_options(hyperwave PRIVATE
            "-Wl,-rpath,@loader_path/../Frameworks"
            "-Wl,-rpath,${SPARKLE_FRAMEWORK_DIR}")
    elseif(WIN32)
        target_include_directories(hyperwave PRIVATE "${WINSPARKLE_DIR}/include")
        target_link_libraries(hyperwave PRIVATE "${WINSPARKLE_DIR}/x64/Release/WinSparkle.lib")
        target_compile_definitions(hyperwave PRIVATE
            HYPERWAVE_APPCAST_URL="${HYPERWAVE_APPCAST_URL_WIN}"
            HYPERWAVE_EDDSA_PUBKEY="${HYPERWAVE_EDDSA_PUBKEY}")
        # WinSparkle ships as a DLL; put it beside the exe so a run from the
        # build tree behaves like the installed one.
        add_custom_command(TARGET hyperwave POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${WINSPARKLE_DIR}/x64/Release/WinSparkle.dll"
                    "$<TARGET_FILE_DIR:hyperwave>"
            VERBATIM)
    endif()
endif()

qt_add_qml_module(hyperwave
    URI hyperwave
    VERSION 1.0
    QML_FILES
        qml/Settings.qml
        qml/Splash.qml
        qml/HypernuclearWordmark.qml
        qml/HyperwaveMark.qml
        qml/HyperwaveLogotype.qml
)

# The splash's artwork, the mark it draws, and the face it sets them in.
#
# Bundled rather than loaded from disk: a splash that depends on files beside
# the binary is one that shows a grey rectangle the first time somebody moves
# the app. The font is bundled for the same reason -- a missing family falls
# back to the system UI face silently, and the panel goes generic without
# anything reporting it.
qt_add_resources(hyperwave "hyperwave_icons"
    PREFIX "/icons"
    BASE "resources"
    FILES
        resources/splash.jpg
        resources/Inter-SemiBold.ttf
)

# The mark, from assets/ where it is authored rather than from a copy.
#
# Its own call because qt_add_resources takes one BASE, and this file lives
# beside the artwork rather than beside the generated icons. Read at runtime by
# the Windows tray glyph, which renders it through QSvgRenderer at whatever size
# the shell asks for. The splash draws the same mark from qml/HyperwaveMark.qml
# instead, because a Shape takes a colour and a rasterised SVG does not.
qt_add_resources(hyperwave "hyperwave_mark"
    PREFIX "/icons"
    BASE "assets"
    FILES assets/hyperwave.svg
)

if(APPLE)
    # Naming the icon in the plist is only half of it: the file also has to be
    # copied into Contents/Resources, and CMake will not infer one from the
    # other. Miss the copy and the plist points at nothing, which shows up as
    # the generic application icon with no error anywhere.
    set(HYPERWAVE_ICNS ${CMAKE_SOURCE_DIR}/resources/hyperwave.icns)
    if(EXISTS ${HYPERWAVE_ICNS})
        target_sources(hyperwave PRIVATE ${HYPERWAVE_ICNS})
        set_source_files_properties(${HYPERWAVE_ICNS}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    else()
        message(STATUS "No resources/hyperwave.icns; run scripts/gen_icons.py")
    endif()

    set_target_properties(hyperwave PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE hyperwave.icns
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
        src/platform/InputCapture.mm
        src/platform/AudioLevelMeter.mm
        src/platform/MacBarPresenter.mm
        src/platform/MacNotchPresenter.mm
        src/platform/MacMenuBuilder.mm
        src/app/ShellMac.mm
        src/update/SparkleUpdater.mm
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

    # ole32   COM, and the PROPVARIANT helpers the device names come back in
    # shell32 SHQueryUserNotificationState, for hiding over fullscreen apps
    # user32  window and message plumbing for the panel
    # version FileDescription lookups, for the Playing list's app names
    target_link_libraries(hyperwave PRIVATE ole32 shell32 user32 version)
endif()

if(HYPERWAVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# A local authoring tool, separate from the shipped menu-bar app. Its only
# extra Qt dependency is Multimedia, for decoding and auditioning soundtracks.
find_package(Qt6 6.11 REQUIRED COMPONENTS Multimedia)

add_library(hyperwave_studio_core STATIC
    RenderSession.cpp RenderSession.h
    ExportJob.cpp ExportJob.h
    AudioLoader.cpp AudioLoader.h
    SystemAudioCapture.cpp SystemAudioCapture.h)
target_link_libraries(hyperwave_studio_core PUBLIC hyperwave_core Qt6::Multimedia)

# Reuse the shipped capture backends, with no status item or microphone UI.
if(APPLE)
    target_compile_options(hyperwave_studio_core PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_sources(hyperwave_studio_core PRIVATE
        ../platform/CoreAudioTapSource.mm ../platform/CoreAudioTapSource.h
        ../platform/AudioCapturePermission.mm ../platform/AudioCapturePermission.h
        ../platform/AudioProcessProbe.mm ../platform/AudioProcessProbe.h
        ../platform/AudioLevelMeter.mm ../platform/AudioLevelMeter.h
        ../platform/InputCapture.mm ../platform/InputCapture.h)
    target_link_libraries(hyperwave_studio_core PRIVATE
        "-framework AppKit" "-framework CoreAudio" "-framework AudioToolbox")
elseif(WIN32)
    target_sources(hyperwave_studio_core PRIVATE ../platform/WasapiSource.cpp)
    target_link_libraries(hyperwave_studio_core PRIVATE ole32)
else()
    target_sources(hyperwave_studio_core PRIVATE ../platform/PulseSource.cpp)
endif()

qt_add_executable(hyperwave_studio
    main.cpp StudioWindow.cpp StudioWindow.h
    ../app/VisualiserControls.cpp ../app/VisualiserControls.h)
target_link_libraries(hyperwave_studio PRIVATE hyperwave_studio_core Qt6::Widgets)
set_target_properties(hyperwave_studio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.hypernuclear.hyperwave.studio
    MACOSX_BUNDLE_BUNDLE_NAME "Hyperwave Studio"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${APP_VERSION_BUILD}")

if(APPLE)
    set_target_properties(hyperwave_studio PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
    if(HYPERWAVE_CODESIGN_IDENTITY)
        add_custom_command(TARGET hyperwave_studio POST_BUILD
            COMMAND codesign --force --sign "${HYPERWAVE_CODESIGN_IDENTITY}"
                --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/hyperwave.entitlements"
                $<TARGET_BUNDLE_DIR:hyperwave_studio>
            VERBATIM)
    endif()
endif()

if(APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/resources/hyperwave.icns")
    target_sources(hyperwave_studio PRIVATE "${CMAKE_SOURCE_DIR}/resources/hyperwave.icns")
    set_target_properties(hyperwave_studio PROPERTIES MACOSX_BUNDLE_ICON_FILE hyperwave.icns)
endif()

if(MSVC)
    target_compile_options(hyperwave_studio PRIVATE /W4 /utf-8)
    target_compile_options(hyperwave_studio_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(hyperwave_studio PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(hyperwave_studio_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(HYPERWAVE_BUILD_TESTS)
    qt_add_executable(tst_studio "${CMAKE_SOURCE_DIR}/tests/tst_studio.cpp")
    target_link_libraries(tst_studio PRIVATE hyperwave_studio_core Qt6::Test Qt6::Widgets)
    add_test(NAME tst_studio COMMAND tst_studio)
    set_tests_properties(tst_studio PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

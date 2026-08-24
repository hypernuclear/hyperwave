# Every test here runs headless and links only hyperlight_core, which is the
# point: the DSP has no scene graph, no window and no platform dependency, so
# the parts of this app that are easy to get subtly wrong are also the parts
# that are cheap to pin down.
function(hyperlight_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE hyperlight_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

hyperlight_test(tst_ring)
hyperlight_test(tst_fft)
hyperlight_test(tst_spectrum)
hyperlight_test(tst_ballistics)

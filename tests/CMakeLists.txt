# Every test here runs headless and links only hyperwave_core, which is the
# point: the DSP has no scene graph, no window and no platform dependency, so
# the parts of this app that are easy to get subtly wrong are also the parts
# that are cheap to pin down.
function(hyperwave_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE hyperwave_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

hyperwave_test(tst_ring)
hyperwave_test(tst_fft)
hyperwave_test(tst_spectrum)
hyperwave_test(tst_ballistics)

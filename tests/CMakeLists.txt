# iio_conv_test(<name> [args...]) - tests link the static library so they can
# reach the internal test hooks in src/internal.h.
function(iio_conv_test name)
	add_executable(${name} ${name}.c)
	target_link_libraries(${name} PRIVATE iio-conv-static)
	target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
	add_test(NAME ${name} COMMAND ${name} ${ARGN})
endfunction()

iio_conv_test(test_smoke)
iio_conv_test(test_plan)
iio_conv_test(test_scalar)
set_source_files_properties(test_scalar.c PROPERTIES COMPILE_FLAGS -ffp-contract=off)

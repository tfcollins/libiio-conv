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

if(IIO_CONV_WITH_LIBIIO)
	iio_conv_test(test_adapter ${CMAKE_CURRENT_SOURCE_DIR}/resources/conv_test.xml)
	iio_conv_test(test_adapter_block ${CMAKE_CURRENT_SOURCE_DIR}/resources/conv_test.xml)
	set_tests_properties(test_adapter_block PROPERTIES SKIP_RETURN_CODE 77)
	iio_conv_test(test_stream ${CMAKE_CURRENT_SOURCE_DIR}/resources/conv_test.xml)
	iio_conv_test(test_stream_fake ${CMAKE_CURRENT_SOURCE_DIR}/resources/conv_test.xml)
endif()
iio_conv_test(test_backends)

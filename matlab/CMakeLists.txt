# The iio_mex gateway. Every source group is compiled twice, as OBJECT libraries, so that
# one group can be built and tested while another is still being written:
#   iio-mex-mock-<part>   against tests/mxmock            (G1; ASan + UBSan)
#   iio-mex-real-<part>   against the real MATLAB headers (G2; linked into iio_mex.mexa64)
# <part> is "core", "registry", "test" or a command group of IIO_MEX_GROUPS.
set(IIO_MEX_PARTS core ${IIO_MEX_GROUPS} registry)
set(IIO_MEX_DEFS MATLAB_MEX_FILE)
if(IIO_CONV_BUILD_TESTS)
	list(APPEND IIO_MEX_PARTS test)
	list(APPEND IIO_MEX_DEFS IIO_MEX_TEST_HOOKS)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(IIO_MEX_ENUMS_C ${CMAKE_CURRENT_BINARY_DIR}/iio_mex_enums.c)
add_custom_command(OUTPUT ${IIO_MEX_ENUMS_C}
	COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_enums.py
		${LIBIIO_INCLUDE_DIR}/iio/iio.h ${IIO_MEX_ENUMS_C}
	DEPENDS tools/gen_enums.py ${LIBIIO_INCLUDE_DIR}/iio/iio.h VERBATIM)
add_custom_target(iio-mex-enums DEPENDS ${IIO_MEX_ENUMS_C})

function(iio_mex_objects variant runtime)
	foreach(part ${IIO_MEX_PARTS})
		if(part STREQUAL core)
			set(src mex/iio_mex.c mex/handles.c mex/mx_util.c ${IIO_MEX_ENUMS_C})
		elseif(part STREQUAL registry)
			set(src mex/registry.c)
		else()
			set(src mex/cmd_${part}.c)
		endif()
		set(tgt iio-mex-${variant}-${part})
		add_library(${tgt} OBJECT ${src})
		add_dependencies(${tgt} iio-mex-enums)
		target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mex ${PROJECT_SOURCE_DIR}/tests)
		target_compile_definitions(${tgt} PUBLIC ${IIO_MEX_DEFS})
		target_compile_options(${tgt} PRIVATE -Werror)   # gate G2 of wave 1: a warning fails the build
		target_link_libraries(${tgt} PUBLIC iio-conv-static ${runtime} ${CMAKE_DL_LIBS})
	endforeach()
endfunction()

if(IIO_CONV_BUILD_TESTS)
	# Sanitizers for the mock build only: the same build directory also makes the real MEX.
	add_library(iio-mex-mockrt INTERFACE)
	target_include_directories(iio-mex-mockrt INTERFACE ${PROJECT_SOURCE_DIR}/tests/mxmock)
	if(IIO_CONV_MEX_SANITIZE AND NOT IIO_CONV_SANITIZE)
		target_compile_options(iio-mex-mockrt INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(iio-mex-mockrt INTERFACE -fsanitize=address,undefined)
	endif()
	iio_mex_objects(mock iio-mex-mockrt)

	# Task 7: G4
	set(strict)
	if(IIO_CONV_API_STRICT)
		set(strict --strict)
	endif()
	add_test(NAME mex_api_map COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_api.py
		--iio-h ${LIBIIO_INCLUDE_DIR}/iio/iio.h ${strict})
	add_test(NAME mex_tools_selftest COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_tools.py)
	set_tests_properties(mex_api_map mex_tools_selftest PROPERTIES LABELS mex)
endif()

if(IIO_CONV_BUILD_MATLAB)
	if(IIO_CONV_SANITIZE)
		message(FATAL_ERROR "IIO_CONV_BUILD_MATLAB cannot be combined with IIO_CONV_SANITIZE: "
			"MATLAB cannot load a sanitized MEX file. The mock tests are sanitized anyway.")
	endif()
	find_package(Matlab REQUIRED)   # needs -DMatlab_ROOT_DIR=<root>; MATLAB is not started
	add_library(iio-mex-realrt INTERFACE)
	target_include_directories(iio-mex-realrt SYSTEM INTERFACE ${Matlab_INCLUDE_DIRS})
	# R2018a = interleaved complex API. (R2025b is not a name matrix.h knows: it does not compile.)
	target_compile_definitions(iio-mex-realrt INTERFACE MATLAB_DEFAULT_RELEASE=R2018a)
	# -fexceptions: mexErrMsgIdAndTxt leaves by throwing a C++ exception through these C frames.
	# -fvisibility=default: the project preset is hidden, which would drop mexFunction.
	target_compile_options(iio-mex-realrt INTERFACE -fexceptions -fvisibility=default)
	iio_mex_objects(real iio-mex-realrt)

	set(objs)
	foreach(part ${IIO_MEX_PARTS})
		list(APPEND objs $<TARGET_OBJECTS:iio-mex-real-${part}>)
	endforeach()
	# Adds c_mexapi_version.c, the export version script, libmex and libmx.
	matlab_add_mex(NAME iio_mex MODULE SRC ${objs} R2018a LINK_TO iio-conv-static ${CMAKE_DL_LIBS})
	target_link_options(iio_mex PRIVATE -Wl,--no-undefined)
	set_target_properties(iio_mex PROPERTIES
		C_VISIBILITY_PRESET default
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})   # matlab/iio_mex.mexa64: addpath('matlab')

	if(IIO_CONV_BUILD_TESTS)
		set(mlibs "LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64:${Matlab_ROOT_DIR}/sys/os/glnxa64")
		add_test(NAME mex_exports COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mex_exports.sh $<TARGET_FILE:iio_mex>)
		add_executable(mex_loadcheck ${PROJECT_SOURCE_DIR}/tests/mex/mex_loadcheck.c)
		target_link_libraries(mex_loadcheck PRIVATE ${CMAKE_DL_LIBS})
		add_test(NAME mex_loadcheck COMMAND mex_loadcheck $<TARGET_FILE:iio_mex>)
		set_tests_properties(mex_loadcheck PROPERTIES ENVIRONMENT "${mlibs}")
		set_tests_properties(mex_exports mex_loadcheck PROPERTIES LABELS mex)
		# Task 7: G3
		add_test(NAME mex_mlint COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mlint.sh)
		set_tests_properties(mex_mlint PROPERTIES ENVIRONMENT "MATLAB_ROOT=${Matlab_ROOT_DIR}" LABELS mex)
		set(IIO_MLINT_MIN_ROOT /usr/local/MATLAB/R2023b CACHE PATH "MATLAB root of the minimum supported release (lint only)")
		if(EXISTS ${IIO_MLINT_MIN_ROOT}/bin/glnxa64/mlint)
			add_test(NAME mex_mlint_r2023b COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mlint.sh)
			set_tests_properties(mex_mlint_r2023b PROPERTIES ENVIRONMENT "MATLAB_ROOT=${IIO_MLINT_MIN_ROOT}" LABELS mex)
		endif()
	endif()
endif()

# The iio_mex gateway. Every source group is compiled twice, as OBJECT libraries, so that
# one group can be built and tested while another is still being written:
#   iio-mex-mock-<part>   against tests/mxmock            (G1; ASan + UBSan)
#   iio-mex-real-<part>   against the real MATLAB headers (G2; linked into iio_mex.mexa64)
# <part> is "core", "registry", "test" or a command group of IIO_MEX_GROUPS.
# The part "test" (cmd_test.c: the test_* commands and the scripted backend of the tests) is
# always in the mock variant, whose tests call it. It is in the MEX file only with
# IIO_CONV_MEX_TEST_HOOKS=ON: a MEX file built without that option is one to hand out.
set(IIO_MEX_PARTS core ${IIO_MEX_GROUPS} registry)
set(IIO_MEX_REAL_PARTS ${IIO_MEX_PARTS})
if(IIO_CONV_MEX_TEST_HOOKS)
	list(APPEND IIO_MEX_REAL_PARTS test)
endif()

set(IIO_MEX_ENUMS_C ${CMAKE_CURRENT_BINARY_DIR}/iio_mex_enums.c)
add_custom_command(OUTPUT ${IIO_MEX_ENUMS_C}
	COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_enums.py
		${LIBIIO_INCLUDE_DIR}/iio/iio.h ${IIO_MEX_ENUMS_C}
	DEPENDS tools/gen_enums.py ${LIBIIO_INCLUDE_DIR}/iio/iio.h VERBATIM)
add_custom_target(iio-mex-enums DEPENDS ${IIO_MEX_ENUMS_C})

# hooks: ON adds the part "test" and defines IIO_MEX_TEST_HOOKS for every part.
function(iio_mex_objects variant runtime hooks)
	set(parts ${IIO_MEX_PARTS})
	set(defs MATLAB_MEX_FILE)
	if(hooks)
		list(APPEND parts test)
		list(APPEND defs IIO_MEX_TEST_HOOKS)
	endif()
	foreach(part ${parts})
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
		target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mex)
		if(part STREQUAL test)
			target_include_directories(${tgt} PRIVATE ${PROJECT_SOURCE_DIR}/tests)   # fake_backend.h
		endif()
		target_compile_definitions(${tgt} PUBLIC ${defs})
		if(IIO_CONV_STRICT_TESTS)
			target_compile_options(${tgt} PRIVATE -Werror)   # gate G2 of wave 1: a warning fails the build
		endif()
		target_link_libraries(${tgt} PUBLIC iio-conv-static ${runtime} ${CMAKE_DL_LIBS})
	endforeach()
endfunction()

if(IIO_MEX_TESTS)
	# Sanitizers for the mock build only: the same build directory also makes the real MEX.
	add_library(iio-mex-mockrt INTERFACE)
	target_include_directories(iio-mex-mockrt INTERFACE ${PROJECT_SOURCE_DIR}/tests/mxmock)
	set(sanitize OFF)
	if(IIO_CONV_MEX_SANITIZE AND NOT IIO_CONV_SANITIZE)
		# The runtime (libasan, libubsan) is a separate package on some systems.
		include(CheckCSourceCompiles)
		include(CMakePushCheckState)
		cmake_push_check_state(RESET)
		set(CMAKE_REQUIRED_FLAGS -fsanitize=address,undefined)
		set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=address,undefined)
		check_c_source_compiles("int main(void) { return 0; }" IIO_MEX_HAVE_SANITIZERS)
		cmake_pop_check_state()
		if(IIO_MEX_HAVE_SANITIZERS)
			set(sanitize ON)
		elseif(IIO_CONV_STRICT_TESTS)
			message(FATAL_ERROR "IIO_CONV_MEX_SANITIZE: the compiler cannot link with -fsanitize=address,undefined")
		else()
			message(STATUS "No ASan/UBSan runtime: the MATLAB gateway tests are built without sanitizers")
		endif()
	endif()
	if(sanitize)
		target_compile_options(iio-mex-mockrt INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(iio-mex-mockrt INTERFACE -fsanitize=address,undefined)
	endif()
	iio_mex_objects(mock iio-mex-mockrt ON)

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
	if(POLICY CMP0144)
		cmake_policy(SET CMP0144 NEW)   # an environment variable MATLAB_ROOT (CI has one) is no warning
	endif()
	find_package(Matlab REQUIRED)   # needs -DMatlab_ROOT_DIR=<root>; MATLAB is not started
	add_library(iio-mex-realrt INTERFACE)
	target_include_directories(iio-mex-realrt SYSTEM INTERFACE ${Matlab_INCLUDE_DIRS})
	# R2018a = interleaved complex API. (R2025b is not a name matrix.h knows: it does not compile.)
	target_compile_definitions(iio-mex-realrt INTERFACE MATLAB_DEFAULT_RELEASE=R2018a)
	# -fexceptions: mexErrMsgIdAndTxt leaves by throwing a C++ exception through these C frames.
	# -fvisibility=default: the project preset is hidden, which would drop mexFunction.
	target_compile_options(iio-mex-realrt INTERFACE -fexceptions -fvisibility=default)
	if(IIO_CONV_MEX_TEST_HOOKS)
		message(STATUS "IIO_CONV_MEX_TEST_HOOKS: iio_mex has the test_* commands; it is a MEX file for the tests, not one to hand out")
	endif()
	iio_mex_objects(real iio-mex-realrt ${IIO_CONV_MEX_TEST_HOOKS})

	set(objs)
	foreach(part ${IIO_MEX_REAL_PARTS})
		list(APPEND objs $<TARGET_OBJECTS:iio-mex-real-${part}>)
	endforeach()
	# Adds c_mexapi_version.c, the export version script, libmex and libmx.
	matlab_add_mex(NAME iio_mex MODULE SRC ${objs} R2018a LINK_TO iio-conv-static ${CMAKE_DL_LIBS})
	target_link_options(iio_mex PRIVATE -Wl,--no-undefined)
	# matlab/iio_mex.mexa64 by default, for addpath('matlab'). Every build directory writes that
	# one file: two full builds at the same time (or for two MATLAB releases) each need their own.
	set(IIO_MEX_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Directory that receives iio_mex.mexa64")
	set_target_properties(iio_mex PROPERTIES
		C_VISIBILITY_PRESET default
		LIBRARY_OUTPUT_DIRECTORY ${IIO_MEX_OUTPUT_DIR})

	if(IIO_CONV_BUILD_TESTS)   # these need neither the mock runtime nor Python
		set(mlibs "LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64:${Matlab_ROOT_DIR}/sys/os/glnxa64")
		add_test(NAME mex_exports COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mex_exports.sh $<TARGET_FILE:iio_mex>)
		add_executable(mex_loadcheck ${PROJECT_SOURCE_DIR}/tests/mex/mex_loadcheck.c)
		target_link_libraries(mex_loadcheck PRIVATE ${CMAKE_DL_LIBS})
		add_test(NAME mex_loadcheck COMMAND mex_loadcheck $<TARGET_FILE:iio_mex>)
		set_tests_properties(mex_loadcheck PROPERTIES ENVIRONMENT "${mlibs}")
		# The test_* commands are in the MEX file exactly when IIO_CONV_MEX_TEST_HOOKS asks for them.
		if(IIO_CONV_MEX_TEST_HOOKS)
			set(hooks with)
		else()
			set(hooks without)
		endif()
		add_test(NAME mex_test_hooks COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mex_hooks.sh $<TARGET_FILE:iio_mex> ${hooks})
		set_tests_properties(mex_exports mex_loadcheck mex_test_hooks PROPERTIES LABELS mex)
		# Task 7: G3
		add_test(NAME mex_mlint COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mlint.sh)
		# The lint tests are exempt from the 4 s rule: mlint of R2023b needs about 0.12 s per file.
		set_tests_properties(mex_mlint PROPERTIES ENVIRONMENT "MATLAB_ROOT=${Matlab_ROOT_DIR}" LABELS mex TIMEOUT 120)
		set(IIO_MLINT_MIN_ROOT /usr/local/MATLAB/R2023b CACHE PATH "MATLAB root of the minimum supported release (lint only)")
		if(EXISTS ${IIO_MLINT_MIN_ROOT}/bin/glnxa64/mlint)
			add_test(NAME mex_mlint_r2023b COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mlint.sh)
			set_tests_properties(mex_mlint_r2023b PROPERTIES ENVIRONMENT "MATLAB_ROOT=${IIO_MLINT_MIN_ROOT}" LABELS mex TIMEOUT 120)
		endif()
	endif()
endif()

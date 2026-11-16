# Gateway tests on the mock MATLAB runtime (G1). All tests here carry the label "mex".
add_library(iio-mex-testrt STATIC ../mxmock/mxmock.c mex_test.c $<TARGET_OBJECTS:iio-mex-mock-core>)
target_include_directories(iio-mex-testrt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(iio-mex-testrt PUBLIC iio-mex-mock-core)

# Every tests/mex/test_*.c is one test executable, run with the fixture XML as argv[1].
# Its line
#     MT_GROUPS(&gw_group_context, &gw_group_attr);
# names the command groups it needs: exactly those cmd_<group>.c objects are linked, so a
# test builds while other groups are still being written. Names that are not gateway
# groups (groups the test defines itself) are ignored here.
file(GLOB mex_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
set(names)
foreach(src ${mex_tests})
	get_filename_component(name ${src} NAME_WE)
	file(STRINGS ${src} line REGEX "^MT_GROUPS\\(" LIMIT_COUNT 1)
	string(REGEX MATCHALL "gw_group_[a-z]+" syms "${line}")
	add_executable(${name} ${src})
	foreach(sym ${syms})
		string(REPLACE "gw_group_" "" g ${sym})
		if(TARGET iio-mex-mock-${g})
			target_sources(${name} PRIVATE $<TARGET_OBJECTS:iio-mex-mock-${g}>)
		endif()
	endforeach()
	target_link_libraries(${name} PRIVATE iio-mex-testrt)
	add_test(NAME ${name} COMMAND ${name} ${IIO_FIXTURE_XML})
	list(APPEND names ${name})
endforeach()

# The mock agrees with the real libmx: one source, both runtimes.
add_executable(mx_conformance_mock mx_conformance.c ../mxmock/mxmock.c)
target_include_directories(mx_conformance_mock PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(mx_conformance_mock PRIVATE iio-mex-mockrt)
add_test(NAME mx_conformance_mock COMMAND mx_conformance_mock)
list(APPEND names mx_conformance_mock)

if(IIO_CONV_BUILD_MATLAB)
	add_executable(mx_conformance_real mx_conformance.c)
	target_include_directories(mx_conformance_real PRIVATE ${PROJECT_SOURCE_DIR}/tests)
	target_include_directories(mx_conformance_real SYSTEM PRIVATE ${Matlab_INCLUDE_DIRS})
	target_compile_definitions(mx_conformance_real PRIVATE MATLAB_DEFAULT_RELEASE=R2018a)
	target_link_libraries(mx_conformance_real PRIVATE ${Matlab_MX_LIBRARY})
	target_link_options(mx_conformance_real PRIVATE -Wl,--allow-shlib-undefined)
	add_test(NAME mx_conformance_real COMMAND mx_conformance_real)
	set_tests_properties(mx_conformance_real PROPERTIES ENVIRONMENT
		"LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64:${Matlab_ROOT_DIR}/sys/os/glnxa64")
	list(APPEND names mx_conformance_real)
	if(TARGET test_slice)
		# libiio bound to MATLAB's bundled libxml2 and libzstd instead of the system ones.
		add_test(NAME mex_bundled_libs COMMAND test_slice ${IIO_FIXTURE_XML})
		set_tests_properties(mex_bundled_libs PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64")
		list(APPEND names mex_bundled_libs)
	endif()
	if(TARGET test_integration)
		# The same for every command group: stale ids, random arguments, allocation failures.
		add_test(NAME mex_bundled_libs_integration COMMAND test_integration ${IIO_FIXTURE_XML})
		set_tests_properties(mex_bundled_libs_integration PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64")
		list(APPEND names mex_bundled_libs_integration)
	endif()
endif()
set_tests_properties(${names} PROPERTIES LABELS mex TIMEOUT 30)
# The dispatcher's own frame on the real stack, which the mock fills with 0xA5 before every
# call: an uninitialised field of struct gw_call then fails this test instead of reading as 0
# from AddressSanitizer's fake stack.
set_tests_properties(test_dispatch PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_stack_use_after_return=0")

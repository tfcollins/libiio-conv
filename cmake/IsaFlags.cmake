# iio_conv_add_backend(<name> [flags...])
# Adds src/kernels/<name>.c to IIO_CONV_SOURCES, compiled with the given flags
# plus -ffp-contract=off, and defines IIO_CONV_HAVE_<NAME> for the library.
macro(iio_conv_add_backend name)
	set(_src ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/${name}.c)
	list(APPEND IIO_CONV_SOURCES ${_src})
	set(_flags -ffp-contract=off ${ARGN})
	string(REPLACE ";" " " _flags "${_flags}")
	set_source_files_properties(${_src} PROPERTIES COMPILE_FLAGS "${_flags}")
	string(TOUPPER ${name} _upper)
	list(APPEND IIO_CONV_DEFINITIONS IIO_CONV_HAVE_${_upper})
endmacro()

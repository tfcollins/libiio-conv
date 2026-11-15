add_executable(bench_conv bench_conv.c)
target_link_libraries(bench_conv PRIVATE iio-conv-static)
target_include_directories(bench_conv PRIVATE ${PROJECT_SOURCE_DIR}/src)

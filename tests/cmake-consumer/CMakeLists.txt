cmake_minimum_required(VERSION 3.16)
project(ChronoPipeConsumer LANGUAGES C)

find_package(ChronoPipe 0.1 REQUIRED CONFIG)
add_executable(chronopipe_consumer main.c)
target_link_libraries(chronopipe_consumer PRIVATE ChronoPipe::chronopipe)

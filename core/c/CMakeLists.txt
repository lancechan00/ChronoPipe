add_library(chronopipe STATIC
  src/arena.c
  src/cpb1.c
  src/cpb2.c
  src/graph.c
  src/recorder.c
  src/stages.c
  src/store_flash.c
  src/store_ram.c
  src/transport.c
  src/types.c
)

target_include_directories(chronopipe PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(chronopipe PUBLIC c_std_99)

if(NOT MSVC)
  target_link_libraries(chronopipe PUBLIC m)
endif()

if(MSVC)
  target_compile_options(chronopipe PRIVATE /W4 /WX)
else()
  target_compile_options(chronopipe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ChronoPipe::chronopipe ALIAS chronopipe)

install(TARGETS chronopipe
  EXPORT ChronoPipeTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

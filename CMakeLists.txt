cmake_minimum_required(VERSION 3.16)
project(ChronoPipe VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(CHRONOPIPE_BUILD_TESTS "Build ChronoPipe tests" ON)
option(CHRONOPIPE_BUILD_EXAMPLES "Build ChronoPipe examples" ON)

add_subdirectory(core/c)

if(CHRONOPIPE_BUILD_EXAMPLES)
  add_subdirectory(examples/host_pipeline)
endif()

configure_package_config_file(
  cmake/ChronoPipeConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ChronoPipeConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoPipe
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ChronoPipeConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(EXPORT ChronoPipeTargets
  FILE ChronoPipeTargets.cmake
  NAMESPACE ChronoPipe::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoPipe
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ChronoPipeConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ChronoPipeConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoPipe
)

if(CHRONOPIPE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/c)
endif()

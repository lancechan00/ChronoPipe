add_executable(chronopipe_pipeline_example main.c)
target_link_libraries(chronopipe_pipeline_example PRIVATE ChronoPipe::chronopipe)
target_compile_features(chronopipe_pipeline_example PRIVATE c_std_99)

if(MSVC)
  target_compile_options(chronopipe_pipeline_example PRIVATE /W4 /WX)
else()
  target_compile_options(chronopipe_pipeline_example PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

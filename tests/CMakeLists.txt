add_executable(djq-tests job_test.cpp)
target_link_libraries(djq-tests PRIVATE djq-core)
target_compile_options(djq-tests PRIVATE -Wall -Wextra)
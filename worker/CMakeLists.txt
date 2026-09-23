add_executable(djq-worker src/main.cpp)
target_link_libraries(djq-worker PRIVATE djq-core)
target_compile_options(djq-worker PRIVATE -Wall -Wextra)
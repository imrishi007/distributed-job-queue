add_executable(djq-backend src/main.cpp)

target_include_directories(djq-backend PRIVATE third_party)

target_compile_options(djq-backend PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(djq-backend PRIVATE Threads::Threads)
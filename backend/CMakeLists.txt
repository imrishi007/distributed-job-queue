find_package(PostgreSQL REQUIRED)

add_library(djq-core src/job.cpp src/redis.cpp src/pq.cpp)
target_include_directories(djq-core PUBLIC src third_party)
target_compile_options(djq-core PRIVATE -Wall -Wextra)
find_package(Threads REQUIRED)
target_link_libraries(djq-core PUBLIC hiredis Threads::Threads PostgreSQL::PostgreSQL)

add_executable(djq-backend src/main.cpp)
target_link_libraries(djq-backend PRIVATE djq-core)
target_compile_options(djq-backend PRIVATE -Wall -Wextra)
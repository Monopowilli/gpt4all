cmake_minimum_required(VERSION 3.28...3.31)
project(gpt4all-backend VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)  # make sure fmt is compiled with the same C++ version as us
include(../common/common.cmake)

find_package(Qt6 6.8 COMPONENTS Concurrent Core Network REQUIRED)

add_subdirectory(../deps common_deps)
add_subdirectory(deps)
add_subdirectory(src)

target_sources(gpt4all-backend PUBLIC
    FILE_SET public_headers TYPE HEADERS BASE_DIRS include FILES
        include/gpt4all-backend/formatters.h
        include/gpt4all-backend/generation-params.h
        include/gpt4all-backend/json-helpers.h
        include/gpt4all-backend/ollama-client.h
        include/gpt4all-backend/ollama-model.h
        include/gpt4all-backend/ollama-types.h
        include/gpt4all-backend/rest.h
)

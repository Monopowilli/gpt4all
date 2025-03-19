cmake_minimum_required(VERSION 3.28...3.31)
project(gpt4all-backend-test VERSION 0.1 LANGUAGES CXX)

set(G4A_TEST_OLLAMA_URL "http://localhost:11434/" CACHE STRING "The base URL of the Ollama server to use.")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include(../common/common.cmake)

add_subdirectory(../gpt4all-backend gpt4all-backend)
add_subdirectory(src)

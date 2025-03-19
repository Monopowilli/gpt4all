set(TARGET test-backend)

configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/config.h")

add_executable(${TARGET}
    main.cpp
)
target_compile_features(${TARGET} PUBLIC cxx_std_23)
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)
gpt4all_add_warning_options(${TARGET})
target_link_libraries(${TARGET} PRIVATE
    gpt4all-backend
)

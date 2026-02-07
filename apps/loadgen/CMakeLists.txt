add_executable(loadgen
    src/main.cpp
    src/LoadgenApplication.cpp
)

target_include_directories(loadgen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/apps/common
)

target_link_libraries(loadgen PRIVATE
    hypernet_runtime
    hyperapp_core
    trading_core
)

add_custom_command(TARGET loadgen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:loadgen>/config
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/client.toml
            $<TARGET_FILE_DIR:loadgen>/config/client.toml
)

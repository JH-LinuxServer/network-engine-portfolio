add_executable(mock_exchange
    src/main.cpp
    src/MockExchangeApplication.cpp
)

target_include_directories(mock_exchange PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/apps/common
)

target_link_libraries(mock_exchange PRIVATE
    hypernet_runtime
    hyperapp_core
    trading_core
)

add_custom_command(TARGET mock_exchange POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:mock_exchange>/config
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/exchange.toml
            $<TARGET_FILE_DIR:mock_exchange>/config/exchange.toml
)

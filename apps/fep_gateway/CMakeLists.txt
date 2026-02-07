add_executable(fep_gateway
    src/main.cpp
    src/FepGatewayApplication.cpp
)

target_include_directories(fep_gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/apps/common
)

target_link_libraries(fep_gateway PRIVATE
    hypernet_runtime
    hyperapp_core
    trading_core
)
 
# config/*.toml을 런타임 출력 디렉토리(bin/) 아래 config/로 복사
add_custom_command(TARGET fep_gateway POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:fep_gateway>/config
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/fep.toml
        $<TARGET_FILE_DIR:fep_gateway>/config/fep.toml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/client.toml
        $<TARGET_FILE_DIR:fep_gateway>/config/client.toml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/exchange.toml
        $<TARGET_FILE_DIR:fep_gateway>/config/exchange.toml
    VERBATIM
)

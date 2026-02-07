add_library(hyperapp_core STATIC
    src/hyperapp/core/AppRuntime.cpp
    src/hyperapp/core/SessionRegistry.cpp
    src/hyperapp/core/SessionService.cpp
    src/hyperapp/core/SessionStateMachine.cpp
    src/hyperapp/core/TopicBroadcaster.cpp
    src/hyperapp/jobs/JobSystem.cpp
)

target_include_directories(hyperapp_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/engine/include
)

target_link_libraries(hyperapp_core
    PUBLIC
        hypernet_engine
)
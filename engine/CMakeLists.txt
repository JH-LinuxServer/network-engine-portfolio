add_library(hypernet_engine STATIC
    src/hypernet/Engine.cpp
    src/hypernet/EngineConfig.cpp

    src/hypernet/core/Logger.cpp
    src/hypernet/core/ConfigLoader.cpp
    src/hypernet/core/LoggingConfig.cpp
    src/hypernet/core/TaskQueue.cpp
    src/hypernet/core/TimerWheel.cpp
    src/hypernet/core/WorkerContext.cpp
    src/hypernet/core/SignalHandler.cpp

    src/hypernet/buffer/RingBuffer.cpp
    src/hypernet/buffer/BufferPool.cpp

    src/hypernet/net/Socket.cpp
    src/hypernet/net/Acceptor.cpp
    src/hypernet/net/EpollReactor.cpp
    src/hypernet/net/EventLoop.cpp
    src/hypernet/net/Session.cpp
    src/hypernet/net/SessionManager.cpp
    src/hypernet/net/SessionRouterFactory.cpp
    src/hypernet/net/WorkerSchedulerFactory.cpp

    src/hypernet/monitoring/Metrics.cpp
    src/hypernet/monitoring/HttpStatusServer.cpp

    src/hypernet/connector/ConnectorManager.cpp
)

target_include_directories(hypernet_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(hypernet_engine
    PUBLIC
        Threads::Threads
)
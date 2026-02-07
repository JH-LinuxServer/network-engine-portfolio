
# # RingBuffer 등 엔진 컴포넌트 테스트를 위한 CMake 설정입니다.
# add_executable(hypernet_tests_ringbuffer
#     buffer/RingBufferTests.cpp
# )

# target_include_directories(hypernet_tests_ringbuffer
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_tests_ringbuffer
#     PRIVATE
#         hypernet_engine
# )

# # BufferPool 테스트 실행 파일
# add_executable(hypernet_tests_buffer_pool
#     buffer/BufferPoolTests.cpp
# )

# target_include_directories(hypernet_tests_buffer_pool
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_tests_buffer_pool
#     PRIVATE
#         hypernet_engine
# )

# # TaskQueue 테스트 실행 파일
# add_executable(hypernet_tests_task_queue
#     core/TaskQueueTests.cpp
# )

# target_include_directories(hypernet_tests_task_queue
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_tests_task_queue
#     PRIVATE
#         hypernet_engine
# )

# # TimerWheel 테스트 실행 파일
# add_executable(hypernet_tests_timer_wheel
#     core/TimerWheelTests.cpp
# )

# target_include_directories(hypernet_tests_timer_wheel
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_tests_timer_wheel
#     PRIVATE
#         hypernet_engine
# )

# # Socket 테스트 실행 파일
# add_executable(hypernet_tests_socket
#     net/SocketTests.cpp
# )

# target_include_directories(hypernet_tests_socket
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_tests_socket
#     PRIVATE
#         hypernet_engine
# )


# # Acceptor 테스트 실행 파일
# add_executable(hypernet_tests_acceptor
#     net/AcceptorTests.cpp
# )

# target_include_directories(hypernet_tests_acceptor
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_tests_acceptor
#     PRIVATE
#         hypernet_engine
# )



# # test_fd_routing 테스트 실행 파일
# add_executable(hypernet_fd_routing_test
#      net/test_fd_routing.cpp
# )

# target_include_directories(hypernet_fd_routing_test
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_fd_routing_test
#     PRIVATE
#         hypernet_engine
# )

# # FramerSmokeTests 테스트 실행 파일
# add_executable(FramerSmokeTests
#      protocol/FramerSmokeTests.cpp
# )

# target_include_directories(FramerSmokeTests
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(FramerSmokeTests
#     PRIVATE
#         hypernet_engine
# )

# # ===== STEP 15-3: protocol/MessageCodec unit tests =====
# add_executable(hypernet_messagecodec_tests
#     protocol/MessageCodecTests.cpp
# )

# target_include_directories(hypernet_messagecodec_tests
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
# )

# target_link_libraries(hypernet_messagecodec_tests
#     PRIVATE
#         hypernet_engine
# )

# # ============================================================
# #  PacketReader/PacketWriter Codec Tests
# # ============================================================
# add_executable(hyperapp_packet_codec_tests
#     protocol/PacketCodecTests.cpp
# )

# target_include_directories(hyperapp_packet_codec_tests
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
#         ${CMAKE_SOURCE_DIR}/runtime/extensions/app/include
# )

# target_link_libraries(hyperapp_packet_codec_tests
#     PRIVATE
#         hyperapp_core
# )





# # ===== Order packet tests (NEW) =====
# add_executable(trading_order_packet_tests
#     protocol/OrderPacketTests.cpp
# )
#  target_include_directories(trading_order_packet_tests
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/engine/include
#         ${CMAKE_SOURCE_DIR}/runtime/extensions/app/include
#         ${CMAKE_SOURCE_DIR}/domains/trading/include
# )
# # ============================================================
# # CTest 등록
# # ============================================================
# add_test(
#     NAME RingBuffer.Basic
#     COMMAND hypernet_tests_ringbuffer
# )

# add_test(
#     NAME BufferPool.Basic
#     COMMAND hypernet_tests_buffer_pool
# )

# add_test(
#     NAME TaskQueue.Basic
#     COMMAND hypernet_tests_task_queue
# )

# add_test(
#     NAME TimerWheel.Basic
#     COMMAND hypernet_tests_timer_wheel
# )

# add_test(
#     NAME Socket.Basic
#     COMMAND hypernet_tests_socket
# )

# add_test(
#     NAME Acceptor.Basic
#     COMMAND hypernet_tests_acceptor
# )


# add_test(
#     NAME hypernet_fd_routing_test
#     COMMAND hypernet_fd_routing_test
# )


# add_test(
#     NAME FramerSmoke.Basic
#     COMMAND FramerSmokeTests  # [FIX] 이전 코드 오타 수정: hypernet_tests_acceptor -> FramerSmokeTests
# )


# add_test(
#     NAME hypernet_messagecodec_tests 
#     COMMAND hypernet_messagecodec_tests
# )


# add_test(
#     NAME hyperapp.packet_codec
#     COMMAND hyperapp_packet_codec_tests
# )




# add_test(
#     NAME trading.order
#     COMMAND trading_order_packet_tests
# )


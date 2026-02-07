# runtime/CMakeLists.txt

# 1. 기존 런타임 (서버 부팅용 껍데기) 
add_library(hypernet_runtime INTERFACE)
target_include_directories(hypernet_runtime INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(hypernet_runtime INTERFACE hypernet_engine)


add_subdirectory(extensions/app) 
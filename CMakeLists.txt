cmake_minimum_required(VERSION 3.16)

project(FEP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# [Fix] CTest 활성화 (반드시 add_subdirectory(tests) 전에 호출해야 함)
enable_testing()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if (ipo_supported)
  option(FEP_ENABLE_LTO "Enable LTO/IPO" OFF)
  if (FEP_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif() 

option(FEP_ENABLE_ASAN "Enable AddressSanitizer" OFF)
if (FEP_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

option(FEP_BIND_FAILFAST "Abort on packet bind contract violations (release-safe)" OFF)
if (FEP_BIND_FAILFAST)
  add_compile_definitions(FEP_BIND_FAILFAST=1)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(engine)
add_subdirectory(runtime)
add_subdirectory(domains/trading)

# Apps (Final Topology)
add_subdirectory(apps/fep_gateway)
add_subdirectory(apps/loadgen)
add_subdirectory(apps/mock_exchange)

add_subdirectory(tests)

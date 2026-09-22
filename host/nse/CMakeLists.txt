cmake_minimum_required(VERSION 3.16)
project(retroslack_nse C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

set(PROTOCOL_DIR ${CMAKE_SOURCE_DIR}/../../protocol)

add_library(rhttp_codec STATIC ${PROTOCOL_DIR}/rhttp_codec.c)
target_include_directories(rhttp_codec PUBLIC ${PROTOCOL_DIR})

add_executable(nse
  main.c
  serial_posix.c
  serial_tcp.c
  http_client.c
  slack_bridge.c
)
target_include_directories(nse PRIVATE ${PROTOCOL_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nse PRIVATE rhttp_codec CURL::libcurl)

add_executable(test_rhttp_codec ${PROTOCOL_DIR}/test_rhttp_codec.c)
target_link_libraries(test_rhttp_codec PRIVATE rhttp_codec)

install(TARGETS nse RUNTIME DESTINATION bin)

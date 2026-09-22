cmake_minimum_required(VERSION 3.14)

# Build with Retro68 toolchain:
#   cmake -S mac -B build/mac -DCMAKE_TOOLCHAIN_FILE=$RETRO68/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   cmake --build build/mac

project(RetroSlackMac C)

include(CMakeParseArguments)

# Guests streamed onto a Plus. Links the runtime that answers
# ping / stop from WaitNextEvent (see common/harness_guest.c).
# Deploy with the sibling HelloMacintosh repo's hellomacintosh CLI
# (https://github.com/kApp-Oliver/HelloMacintosh — make bootstrap clones it).
function(add_harness_guest name)
    cmake_parse_arguments(HG "" "TYPE;CREATOR" "" ${ARGN})
    if(NOT HG_TYPE)
        set(HG_TYPE "APPL")
    endif()
    if(NOT HG_CREATOR)
        set(HG_CREATOR "????")
    endif()
    add_application(${name}
        ${HG_UNPARSED_ARGUMENTS}
        common/harness.c
        common/harness_guest.c
        common/ser_modem.c
        common/size_multifinder.r
        TYPE "${HG_TYPE}"
        CREATOR "${HG_CREATOR}"
    )
    target_include_directories(${name} PRIVATE
        common ${CMAKE_SOURCE_DIR}/../protocol)
endfunction()

add_application(RHTTPTest
    rhttp_test/main.c
    common/rhttp_client.c
    common/rhttp_debug.c
    TYPE "APPL"
    CREATOR "RhtT"
)

target_include_directories(RHTTPTest PRIVATE common ${CMAKE_SOURCE_DIR}/../protocol)

add_harness_guest(RetroSlack
    retroslack/main.c
    common/rhttp_client.c
    common/rhttp_debug.c
    common/mini_json.c
    TYPE "APPL"
    CREATOR "RSlk"
)

# Optional debug logging on printer port
option(RHTTP_DEBUG "Enable printer-port debug logs" OFF)
if(RHTTP_DEBUG)
  target_compile_definitions(RHTTPTest PRIVATE RHTTP_DEBUG=1)
  target_compile_definitions(RetroSlack PRIVATE RHTTP_DEBUG=1)
endif()

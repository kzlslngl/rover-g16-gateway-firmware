cmake_minimum_required(VERSION 3.16)
project(rover_g16_host_tests C)

enable_testing()

add_executable(core_tests
    core_tests.c
    ../components/crc32/crc32_iso_hdlc.c
    ../components/freshness/freshness.c
    ../components/register_image/register_endian.c
    ../components/register_image/register_image.c
    ../components/sbus_decoder/sbus_decoder.c
    ../components/session_id/session_id.c)

target_compile_features(core_tests PRIVATE c_std_11)
target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(core_tests PRIVATE
    ../protocol
    ../components/crc32/include
    ../components/freshness/include
    ../components/register_image/include
    ../components/sbus_decoder/include
    ../components/session_id/include)

add_test(NAME core_tests COMMAND core_tests)

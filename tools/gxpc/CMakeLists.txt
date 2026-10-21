cmake_minimum_required(VERSION 3.16)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
project(gxpc C)

set(VITA_APP_NAME "gxpc")
set(VITA_TITLEID "OMRI00099")
set(VITA_VERSION "01.00")

add_executable(gxpc main.c)
target_compile_options(gxpc PRIVATE -O2 -Wall)
target_link_libraries(gxpc SceShaccCg_stub_weak SceLibKernel_stub)
vita_create_self(eboot.bin gxpc UNSAFE)
vita_create_vpk(gxpc.vpk ${VITA_TITLEID} eboot.bin VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME})

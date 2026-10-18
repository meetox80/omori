cmake_minimum_required(VERSION 3.16)
project(omori_builder C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/src")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(ENGINE_PBP "${CMAKE_CURRENT_SOURCE_DIR}/../../build/psp/EBOOT.PBP" CACHE FILEPATH "engine EBOOT.PBP to embed")
set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")

add_executable(omori_embed tools/embed.c)

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})
if(EXISTS ${ENGINE_PBP})
  add_custom_command(OUTPUT ${GEN}/engine_blob.c
    COMMAND omori_embed ${ENGINE_PBP} ${GEN}/engine_blob.c embedded_engine
    DEPENDS omori_embed ${ENGINE_PBP})
else()
  file(WRITE ${GEN}/engine_blob.c "#include <stddef.h>\nconst unsigned char embedded_engine[1] = {0};\nconst size_t embedded_engine_size = 0;\n")
endif()

set(FALLBACK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fallback_fonts)
set(FALLBACK_BLOBS)
foreach(pair "NotoSans-Fallback.ttf:fallback_noto_sans" "NotoSansSymbols2-Fallback.ttf:fallback_noto_symbols2"
             "NotoSansMath-Fallback.ttf:fallback_noto_math" "NotoSansArabic-Fallback.ttf:fallback_noto_arabic" "NotoSansSC-Fallback.otf:fallback_noto_sc")
  string(REPLACE ":" ";" parts ${pair})
  list(GET parts 0 font_file)
  list(GET parts 1 font_sym)
  add_custom_command(OUTPUT ${GEN}/${font_sym}.c
    COMMAND omori_embed ${FALLBACK_DIR}/${font_file} ${GEN}/${font_sym}.c ${font_sym}
    DEPENDS omori_embed ${FALLBACK_DIR}/${font_file})
  list(APPEND FALLBACK_BLOBS ${GEN}/${font_sym}.c)
endforeach()

file(GLOB_RECURSE VPX_SRC CONFIGURE_DEPENDS third_party/libvpx/*.c)
add_library(omori_vpx STATIC ${VPX_SRC})
target_include_directories(omori_vpx PUBLIC third_party/libvpx)
target_compile_definitions(omori_vpx PRIVATE _DEFAULT_SOURCE)
target_compile_options(omori_vpx PRIVATE -w)

file(GLOB_RECURSE OPUS_SRC CONFIGURE_DEPENDS third_party/opus/*.c)
add_library(omori_opus STATIC ${OPUS_SRC})
target_include_directories(omori_opus PUBLIC third_party/opus/include
  PRIVATE third_party/opus/celt third_party/opus/silk third_party/opus/silk/float third_party/opus/src)
target_compile_definitions(omori_opus PRIVATE OPUS_BUILD HAVE_LRINTF)
if(MSVC)
  target_compile_definitions(omori_opus PRIVATE USE_ALLOCA)
else()
  target_compile_definitions(omori_opus PRIVATE VAR_ARRAYS)
endif()
target_compile_options(omori_opus PRIVATE -w)

file(GLOB BUILDER_SRC CONFIGURE_DEPENDS src/*.c src/*.cpp)
list(APPEND BUILDER_SRC ${SHARED_DIR}/rx.c ${SHARED_DIR}/js.c ${SHARED_DIR}/node.c ${SHARED_DIR}/buf.c ${SHARED_DIR}/script.c ${ENGINE_DIR}/js/value.cpp ${ENGINE_DIR}/js/vm.cpp ${ENGINE_DIR}/js/builtins.cpp ${ENGINE_DIR}/js/random.cpp
     ${ENGINE_DIR}/js/regexp.cpp ${ENGINE_DIR}/js/compile.cpp ${ENGINE_DIR}/js/dataview.cpp ${ENGINE_DIR}/core/value.cpp)
add_executable(omori-builder ${BUILDER_SRC} third_party/miniz.c ${GEN}/engine_blob.c ${FALLBACK_BLOBS})
target_include_directories(omori-builder PRIVATE src third_party ${SHARED_DIR} ${ENGINE_DIR})
target_compile_definitions(omori-builder PRIVATE _DEFAULT_SOURCE _FILE_OFFSET_BITS=64 MINIZ_NO_STDIO)
target_compile_options(omori-builder PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_definitions(omori-builder PRIVATE OMORI_BUILDER=1)
if(WIN32)
  target_link_libraries(omori-builder PRIVATE ole32 shell32 user32 gdi32 advapi32 uuid)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(omori-builder PRIVATE Threads::Threads m)
endif()
target_link_libraries(omori-builder PRIVATE omori_vpx omori_opus)

set(LIB_SRC ${BUILDER_SRC})
list(FILTER LIB_SRC EXCLUDE REGEX "/main\\.c$")
add_executable(omori-builder-tests tests/test.c ${LIB_SRC} third_party/miniz.c ${FALLBACK_BLOBS})
target_include_directories(omori-builder-tests PRIVATE src third_party ${SHARED_DIR} ${ENGINE_DIR})
target_compile_definitions(omori-builder-tests PRIVATE _DEFAULT_SOURCE MINIZ_NO_STDIO)
if(WIN32)
  target_link_libraries(omori-builder-tests PRIVATE ole32 shell32 user32 gdi32 advapi32 uuid)
else()
  target_link_libraries(omori-builder-tests PRIVATE Threads::Threads m)
endif()
target_link_libraries(omori-builder-tests PRIVATE omori_vpx omori_opus)
enable_testing()
add_test(NAME builder COMMAND omori-builder-tests)

cmake_minimum_required(VERSION 3.20)
project(marvin-chess VERSION 7.0.0 LANGUAGES C)

add_executable(marvin
          src/bitboard.c
          src/data.c
          src/debug.c
          src/egtb.c
          src/engine.c
          src/eval.c
          src/evalparams.c
          src/fen.c
          src/hash.c
          src/history.c
          src/key.c
          src/main.c
          src/movegen.c
          src/moveselect.c
          src/nnue.c
          src/polybook.c
          src/position.c
          src/search.c
          src/see.c
          src/sfen.c
          src/simd.c
          src/smp.c
          src/test.c
          src/thread.c
          src/timectl.c
          src/uci.c
          src/utils.c
          src/validation.c
          src/xboard.c
          import/fathom/tbprobe.c
)

include_directories(import/fathom import/incbin)

target_link_libraries(marvin m pthread)

add_compile_definitions(APP_ARCH="x86-64-modern")
add_compile_definitions(APP_VERSION="7.0.0-5")
add_compile_definitions(NETFILE_NAME="best36.nnue")
add_compile_definitions(USE_SSE)
add_compile_definitions(IS_64BIT)
add_compile_definitions(USE_POPCNT)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Werror -Wno-array-bounds -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64 -mpopcnt -msse -msse2 -msse3 -mssse3 -msse4.1")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -funroll-loops -fomit-frame-pointer -flto")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -flto -m64")
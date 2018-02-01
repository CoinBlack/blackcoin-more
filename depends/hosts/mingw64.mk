mingw64_CC = gcc.exe
mingw64_CXX = g++.exe
mingw64_AR = ar.exe
mingw64_RANLIB = ranlib.exe
mingw64_STRIP = strip.exe
mingw64_NM = nm.exe
mingw64_SHA256SUM = sha256sum.exe


mingw64_CFLAGS=-pipe -DPTW32_STATIC_LIB
mingw64_CXXFLAGS=-pipe -DPTW32_STATIC_LIB

mingw64_release_CFLAGS=-O2
mingw64_release_CXXFLAGS=$(mingw64_release_CFLAGS)

mingw64_debug_CFLAGS=-O1
mingw64_debug_CXXFLAGS=$(mingw64_debug_CFLAGS)

mingw64_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC
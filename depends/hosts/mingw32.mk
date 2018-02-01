mingw32_CC = gcc.exe
mingw32_CXX = g++.exe
mingw32_AR = ar.exe
mingw32_RANLIB = ranlib.exe
mingw32_STRIP = strip.exe
mingw32_NM = nm.exe
mingw22_SHA256SUM = sha256sum.exe


mingw32_CFLAGS=-pipe -DPTW32_STATIC_LIB
mingw32_CXXFLAGS=-pipe -DPTW32_STATIC_LIB

mingw32_release_CFLAGS=-O2
mingw32_release_CXXFLAGS=$(mingw32_release_CFLAGS)

mingw32_debug_CFLAGS=-O1
mingw32_debug_CXXFLAGS=$(mingw32_debug_CFLAGS)

mingw32_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC
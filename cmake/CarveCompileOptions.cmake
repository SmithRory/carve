include_guard(GLOBAL)

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g" CACHE STRING "C++ flags for Debug builds." FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "C++ flags for Release builds." FORCE)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON CACHE BOOL "Treat compiler warnings as errors." FORCE)

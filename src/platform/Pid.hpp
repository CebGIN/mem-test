#ifndef MEMTEST_PLATFORM_PID_HPP
#define MEMTEST_PLATFORM_PID_HPP

// `pid_t` portable. En Linux/macOS y en MinGW ya existe; MSVC no lo define.
// Los PID de Windows son DWORD, pero en la práctica caben en un int.
#include <sys/types.h>

#if defined(_MSC_VER)
using pid_t = int;
#endif

#endif  // MEMTEST_PLATFORM_PID_HPP

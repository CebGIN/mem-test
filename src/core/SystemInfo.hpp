#ifndef MEMTEST_CORE_SYSTEMINFO_HPP
#define MEMTEST_CORE_SYSTEMINFO_HPP

#include "domain/Types.hpp"

namespace memtest {

// Lee RAM total, RAM disponible, swap y núcleos del sistema. Se calcula una vez
// al iniciar y alimenta los guardrails.
SystemInfo read_system_info();

}  // namespace memtest

#endif  // MEMTEST_CORE_SYSTEMINFO_HPP

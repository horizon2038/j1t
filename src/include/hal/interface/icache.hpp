#ifndef J1T_HAL_INTERFACE_ICACHE_HPP
#define J1T_HAL_INTERFACE_ICACHE_HPP

#include <stdint.h>

namespace j1t::hal
{
    auto flush_instruction_cache(void *begin, uintmax_t size) -> void;
}

#endif

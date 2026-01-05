#ifndef J1T_HAL_INTERFACE_EXECUTABLE_MEMORY_HPP
#define J1T_HAL_INTERFACE_EXECUTABLE_MEMORY_HPP

#include <stdint.h>

namespace j1t::hal
{
    class executable_memory;

    class executable_memory
    {
      public:
        virtual ~executable_memory(void)           = default;

        virtual auto data(void) -> uint8_t *       = 0;
        virtual auto size(void) const -> uintmax_t = 0;

        virtual auto begin_write(void) -> void     = 0;
        virtual auto end_write(void) -> void       = 0;

        virtual auto finalize(void) -> void        = 0;
    };
}

#endif

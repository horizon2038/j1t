#ifndef J1T_HAL_INTERFACE_COMPILE_HPP
#define J1T_HAL_INTERFACE_COMPILE_HPP

#include <expected>
#include <stdint.h>

namespace j1t::hal
{
    enum class compile_error
    {
        NONE,
        ALLOCATION_FAILED,
        INVALID_INSTRUCTION,
        FINALIZATION_FAILED,
    };

    template<typename T>
    using compile_result = std::expected<T, compile_error>;

    auto compile(void *buffer, uintmax_t size) -> compile_result<void *>;
}

#endif

#ifndef J1T_HAL_AARCH64_EXECUTABLE_MEMORY_MACOS_HPP
#define J1T_HAL_AARCH64_EXECUTABLE_MEMORY_MACOS_HPP

#include <hal/interface/executable_memory.hpp>

namespace j1t::hal::aarch64
{
    class executable_memory_macos final : public j1t::hal::executable_memory
    {
      public:
        executable_memory_macos(uintmax_t size);
        ~executable_memory_macos(void) override;

        auto data(void) -> uint8_t * override;
        auto size(void) const -> uintmax_t override;

        auto begin_write(void) -> void override;
        auto end_write(void) -> void override;

        auto finalize(void) -> void override;

      private:
        uint8_t  *memory_internal { nullptr };
        uintmax_t size_internal { 0 };
    };
}

#endif

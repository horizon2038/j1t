#ifndef J1T_HAL_INTERFACE_JIT_BACKEND_HPP
#define J1T_HAL_INTERFACE_JIT_BACKEND_HPP

#include <memory>
#include <vm/interpreter.hpp>

namespace j1t::hal
{
    // unsafe context for JIT-compiled code
    struct jit_context
    {
        uint8_t  *memory { nullptr };
        uint32_t *stack_base { nullptr };
        uint32_t *stack_top { nullptr };
        uint32_t *locals { nullptr };
    };

    class compiled_code
    {
      public:
        using entry_type                       = uint32_t (*)(jit_context *);

        virtual ~compiled_code(void)           = default;

        virtual auto entry(void) -> entry_type = 0;
        virtual auto code_size(void) const -> uint32_t = 0;
    };

    class jit_backend
    {
      public:
        virtual ~jit_backend(void) = default;

        virtual auto compile(const vm::program &prog)
            -> std::unique_ptr<compiled_code>
            = 0;
    };

    auto make_native_jit_backend(void) -> std::unique_ptr<jit_backend>;
}

#endif

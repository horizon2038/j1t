#ifndef J1T_JIT_ENGINE_HPP
#define J1T_JIT_ENGINE_HPP

#include <memory>

#include <hal/interface/jit_backend.hpp>
#include <vm/interpreter.hpp>

namespace j1t::jit
{
    class engine;

    class engine
    {
      public:
        engine(void) : backend(j1t::hal::make_native_jit_backend())
        {
        }

        auto run(const j1t::vm::program &program, j1t::vm::state &state)
            -> j1t::vm::interpreter::result<>
        {
            if (!backend)
            {
                return std::unexpected(j1t::vm::interpreter::error::INVALID_OPCODE);
            }

            auto compiled = backend->compile(program);
            if (!compiled)
            {
                return std::unexpected(j1t::vm::interpreter::error::INVALID_OPCODE);
            }

            j1t::hal::jit_context ctx {};
            ctx.memory = state.memory.empty() ? nullptr : state.memory.data();
            ctx.stack_base = state.stack.empty() ? nullptr : state.stack.data();
            ctx.stack_top  = state.stack.empty() ?
                                 nullptr :
                                 state.stack.data() + state.stack.size();
            ctx.locals   = state.locals.empty() ? nullptr : state.locals.data();

            uint32_t ret = compiled->entry()(&ctx);

            if (ctx.stack_base != nullptr && ctx.stack_top != nullptr)
            {
                std::ptrdiff_t diff = ctx.stack_top - ctx.stack_base;
                if (diff < 0)
                {
                    return std::unexpected(
                        j1t::vm::interpreter::error::STACK_UNDERFLOW
                    );
                }

                state.stack.resize(static_cast<std::size_t>(diff));
            }

            j1t::vm::interpreter::execution_info info {
                .pc           = 0,
                .return_value = ret,
            };

            return info;
        }

      private:
        std::unique_ptr<j1t::hal::jit_backend> backend;
    };
}

#endif

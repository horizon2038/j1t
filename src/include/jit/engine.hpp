#ifndef J1T_JIT_ENGINE_HPP
#define J1T_JIT_ENGINE_HPP

#include <memory>

#include <hal/interface/jit_backend.hpp>
#include <print>
#include <util/time.hpp>
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

        auto run(const j1t::vm::program &program, j1t::vm::state &state) -> j1t::vm::interpreter::result<>
        {
            if (!backend)
            {
                return std::unexpected(j1t::vm::interpreter::error::INVALID_OPCODE);
            }

            auto compiled = util::calculate_time(
                [&]() -> std::unique_ptr<j1t::hal::compiled_code>
                {
                    std::print("JIT compiling...\n");
                    return backend->compile(program);
                }
            );
            // auto compiled = backend->compile(program);
            if (!compiled)
            {
                return std::unexpected(j1t::vm::interpreter::error::INVALID_OPCODE);
            }

            static constexpr uintmax_t STACK_CAPACITY_WORDS = 4096;

            if (state.stack.size() < STACK_CAPACITY_WORDS)
            {
                state.stack.resize(STACK_CAPACITY_WORDS);
            }

            j1t::hal::jit_context ctx {};
            ctx.memory     = state.memory.empty() ? nullptr : state.memory.data();
            ctx.stack_base = state.stack.empty() ? nullptr : state.stack.data();
            ctx.stack_top  = ctx.stack_base;
            ctx.stack_end  = ctx.stack_base + static_cast<std::ptrdiff_t>(state.stack.size());
            ctx.locals     = state.locals.empty() ? nullptr : state.locals.data();
            ctx.error_code = 0;

            // uint32_t ret   = compiled->entry()(&ctx);
            auto ret = util::calculate_time(
                [&]() -> uint32_t
                {
                    std::print("JIT executing...\n");
                    return compiled->entry()(&ctx);
                }
            );
            if (ctx.error_code != 0)
            {
                switch (ctx.error_code)
                {
                    case 1 :
                        return std::unexpected(j1t::vm::interpreter::error::STACK_UNDERFLOW);

                    case 2 :
                        [[fallthrough]];
                    default :
                        return std::unexpected(j1t::vm::interpreter::error::INVALID_OPCODE);
                }
            }

            if (ctx.stack_base != nullptr && ctx.stack_top != nullptr)
            {
                std::ptrdiff_t diff = ctx.stack_top - ctx.stack_base;
                if (diff < 0)
                {
                    return std::unexpected(j1t::vm::interpreter::error::STACK_UNDERFLOW);
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

#include <hal/interface/icache.hpp>
#include <hal/interface/jit_backend.hpp>

#include <hal/aarch64/executable_memory_macos.hpp>
#include <hal/aarch64/macro_assembler.hpp>

#include <vm/opcodes.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    auto read_u8(const std::vector<uint8_t> &code, uint32_t &program_counter)
        -> uint8_t
    {
        if (program_counter >= code.size())
        {
            throw std::runtime_error("read_u8: program counter out of range");
        }

        return code[program_counter++];
    }

    auto read_u32_le(const std::vector<uint8_t> &code, uint32_t &program_counter)
        -> uint32_t
    {
        if (program_counter + 4u > code.size())
        {
            throw std::runtime_error("read_u32_le: program counter out of range");
        }

        uint32_t byte0 = static_cast<uint32_t>(read_u8(code, program_counter));
        uint32_t byte1 = static_cast<uint32_t>(read_u8(code, program_counter));
        uint32_t byte2 = static_cast<uint32_t>(read_u8(code, program_counter));
        uint32_t byte3 = static_cast<uint32_t>(read_u8(code, program_counter));

        return (byte0 << 0u) | (byte1 << 8u) | (byte2 << 16u) | (byte3 << 24u);
    }
}

namespace j1t::hal
{
    namespace
    {
        class compiled_code_aarch64 final : public compiled_code
        {
          public:
            compiled_code_aarch64(
                std::unique_ptr<j1t::hal::executable_memory> memory,
                uint32_t                                     code_size
            )
                : memory_internal(std::move(memory))
                , code_size_internal(code_size)
            {
            }

            auto entry(void) -> entry_type override
            {
                return reinterpret_cast<entry_type>(memory_internal->data());
            }

            auto code_size(void) const -> uint32_t override
            {
                return code_size_internal;
            }

          private:
            std::unique_ptr<j1t::hal::executable_memory> memory_internal;
            uint32_t code_size_internal { 0 };
        };

        class jit_backend_aarch64 final : public j1t::hal::jit_backend
        {
          public:
            auto compile(const j1t::vm::program &target_program)
                -> std::unique_ptr<j1t::hal::compiled_code> override
            {
                auto memory
                    = std::make_unique<j1t::hal::aarch64::executable_memory_macos>(
                        4096 * 2u
                    );
                memory->begin_write();
                j1t::hal::aarch64::macro_assembler assembler;
                assembler.set_output(*memory);

                constexpr uint32_t REGISTER_CONTEXT   = 0;
                constexpr uint32_t REGISTER_STACK_TOP = 1;
                constexpr uint32_t REGISTER_TEMP_W2   = 2;
                constexpr uint32_t REGISTER_TEMP_W3   = 3;
                constexpr uint32_t REGISTER_RET_W0    = 0;

                constexpr int32_t OFFSET_MEMORY       = 0;
                constexpr int32_t OFFSET_STACK_BASE
                    = static_cast<int32_t>(sizeof(void *));
                constexpr int32_t OFFSET_STACK_TOP
                    = static_cast<int32_t>(sizeof(void *) * 2);
                constexpr int32_t OFFSET_LOCALS
                    = static_cast<int32_t>(sizeof(void *) * 3);

                assembler.emit_load_pointer_from_base_plus_offset(
                    REGISTER_STACK_TOP,
                    REGISTER_CONTEXT,
                    OFFSET_STACK_TOP
                );

                uint32_t pc { 0 };

                while (pc < target_program.code.size())
                {
                    uint8_t opcode_u8 = read_u8(target_program.code, pc);
                    j1t::vm::opcode op = static_cast<j1t::vm::opcode>(opcode_u8);

                    switch (op)
                    {
                        case j1t::vm::opcode::NOP :
                            break;

                        case j1t::vm::opcode::PUSH :
                            {
                                uint32_t immediate_value
                                    = read_u32_le(target_program.code, pc);
                                break;
                            }

                        case j1t::vm::opcode::ADD :
                            {
                                // pop rhs
                                assembler.emit_subtract_immediate_from_pointer(
                                    REGISTER_STACK_TOP,
                                    REGISTER_STACK_TOP,
                                    4u
                                );
                                assembler.emit_load_u32_from_base_plus_offset(
                                    REGISTER_TEMP_W2,
                                    REGISTER_STACK_TOP,
                                    0
                                );

                                // pop lhs
                                assembler.emit_subtract_immediate_from_pointer(
                                    REGISTER_STACK_TOP,
                                    REGISTER_STACK_TOP,
                                    4u
                                );
                                assembler.emit_load_u32_from_base_plus_offset(
                                    REGISTER_TEMP_W3,
                                    REGISTER_STACK_TOP,
                                    0
                                );

                                // w3 += w2
                                assembler.emit_add_u32_register(
                                    REGISTER_TEMP_W3,
                                    REGISTER_TEMP_W3,
                                    REGISTER_TEMP_W2
                                );

                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W3,
                                    REGISTER_STACK_TOP,
                                    0
                                );

                                assembler.emit_add_immediate_to_pointer(
                                    REGISTER_STACK_TOP,
                                    REGISTER_STACK_TOP,
                                    4u
                                );
                                break;
                            }

                        case j1t::vm::opcode::RET :
                            {
                                assembler.emit_subtract_immediate_from_pointer(
                                    REGISTER_STACK_TOP,
                                    REGISTER_STACK_TOP,
                                    4u
                                );
                                assembler.emit_load_u32_from_base_plus_offset(
                                    REGISTER_RET_W0,
                                    REGISTER_STACK_TOP,
                                    0
                                );

                                assembler.emit_store_pointer_from_register_to_base_plus_offset(
                                    REGISTER_STACK_TOP,
                                    REGISTER_CONTEXT,
                                    OFFSET_STACK_TOP
                                );

                                assembler.emit_return();
                                pc = static_cast<uint32_t>(
                                    target_program.code.size()
                                );
                                break;
                            }

                        default :
                            {
                                throw std::runtime_error(
                                    "jit_backend_aarch64: unsupported opcode"
                                );
                            }
                    }
                }

                assembler.emit_move_immediate_u32(REGISTER_RET_W0, 0u);
                assembler.emit_store_pointer_from_register_to_base_plus_offset(
                    REGISTER_STACK_TOP,
                    REGISTER_CONTEXT,
                    OFFSET_STACK_TOP
                );
                assembler.emit_return();
                assembler.finalize();

                memory->end_write();
                uintmax_t used_size = assembler.code_size_bytes();
                j1t::hal::flush_instruction_cache(memory->data(), used_size);
                memory->finalize();

                return std::make_unique<compiled_code_aarch64>(
                    std::move(memory),
                    static_cast<uint32_t>(used_size)
                );
            }
        };
    }

    auto make_native_jit_backend(void) -> std::unique_ptr<jit_backend>
    {
        return std::make_unique<jit_backend_aarch64>();
    }
}

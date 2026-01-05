#ifndef J1T_VM_EMITTER_HPP
#define J1T_VM_EMITTER_HPP

#include <vm/interpreter.hpp>
#include <vm/opcodes.hpp>

namespace j1t::vm
{
    inline constexpr auto emit_u8(std::vector<uint8_t> &code, uint8_t value) -> void
    {
        code.push_back(value);
    }

    inline constexpr auto emit_u32_le(std::vector<uint8_t> &code, uint32_t value)
        -> void
    {
        code.push_back(static_cast<uint8_t>(value & 0xFF));
        code.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        code.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        code.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    inline constexpr auto emit_i32_le(std::vector<uint8_t> &code, int32_t value)
        -> void
    {
        emit_u32_le(code, static_cast<uint32_t>(value));
    }

    inline constexpr auto
        patch_i32_le(std::vector<uint8_t> &code, std::size_t position, int32_t value)
            -> void
    {
        code[position + 0]
            = static_cast<uint8_t>((static_cast<uint32_t>(value) >> 0) & 0xffu);
        code[position + 1]
            = static_cast<uint8_t>((static_cast<uint32_t>(value) >> 8) & 0xffu);
        code[position + 2]
            = static_cast<uint8_t>((static_cast<uint32_t>(value) >> 16) & 0xffu);
        code[position + 3]
            = static_cast<uint8_t>((static_cast<uint32_t>(value) >> 24) & 0xffu);
    }

    inline constexpr auto emit_op(std::vector<uint8_t> &code, j1t::vm::opcode op)
        -> void
    {
        emit_u8(code, j1t::vm::op_to_raw(op));
    }

    inline constexpr auto emit_push(std::vector<uint8_t> &code, uint32_t value)
        -> void
    {
        emit_op(code, j1t::vm::opcode::PUSH);
        emit_u32_le(code, value);
    }

    inline constexpr auto
        emit_local_get(std::vector<uint8_t> &code, uint32_t local_index) -> void
    {
        emit_op(code, j1t::vm::opcode::LOCAL_GET);
        emit_u32_le(code, local_index);
    }

    inline constexpr auto
        emit_local_set(std::vector<uint8_t> &code, uint32_t local_index) -> void
    {
        emit_op(code, j1t::vm::opcode::LOCAL_SET);
        emit_u32_le(code, local_index);
    }

    inline constexpr auto emit_add(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::ADD);
    }

    inline constexpr auto emit_sub(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::SUB);
    }

    inline constexpr auto emit_mul(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::MUL);
    }

    inline constexpr auto emit_div(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::DIV);
    }

    inline constexpr auto emit_eq(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::EQ);
    }

    inline constexpr auto emit_load8_u(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::LOAD_8_UNSIGNED);
    }

    inline constexpr auto emit_load16_u(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::LOAD_16_UNSIGNED);
    }

    inline constexpr auto emit_load32(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::LOAD_32);
    }

    inline constexpr auto
        emit_jump(std::vector<uint8_t> &code, int32_t relative_offset) -> void
    {
        emit_op(code, j1t::vm::opcode::JUMP);
        emit_i32_le(code, relative_offset);
    }

    inline constexpr auto
        emit_jump_if_zero(std::vector<uint8_t> &code, int32_t relative_offset)
            -> void
    {
        emit_op(code, j1t::vm::opcode::JUMP_IF_ZERO);
        emit_i32_le(code, relative_offset);
    }

    inline constexpr auto
        emit_jump_if_not_zero(std::vector<uint8_t> &code, int32_t relative_offset)
            -> void
    {
        emit_op(code, j1t::vm::opcode::JUMP_IF_NOT_ZERO);
        emit_i32_le(code, relative_offset);
    }

    inline constexpr auto emit_print(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::PRINT);
    }

    inline constexpr auto emit_ret(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::RET);
    }

    inline constexpr auto
        emit_print_literal(std::vector<uint8_t> &code, const char *str) -> void
    {
        for (const char *p = str; *p != '\0'; ++p)
        {
            emit_push(code, static_cast<uint8_t>(*p));
            emit_print(code);
        }
    }
}

#endif

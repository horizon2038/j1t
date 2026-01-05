#include <jit/engine.hpp>
#include <vm/interpreter.hpp>
#include <vm/opcodes.hpp>

#include <cstdio>
#include <stdint.h>
#include <vector>

namespace
{
    auto emit_u8(std::vector<uint8_t> &code, uint8_t v) -> void
    {
        code.push_back(v);
    }

    auto emit_u32_le(std::vector<uint8_t> &code, uint32_t v) -> void
    {
        code.push_back(static_cast<uint8_t>((v >> 0) & 0xFFu));
        code.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        code.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        code.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }

    auto emit_op(std::vector<uint8_t> &code, j1t::vm::opcode op) -> void
    {
        emit_u8(code, j1t::vm::op_to_raw(op));
    }

    auto emit_push(std::vector<uint8_t> &code, uint32_t imm) -> void
    {
        emit_op(code, j1t::vm::opcode::PUSH);
        emit_u32_le(code, imm);
    }

    auto emit_add(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::ADD);
    }

    auto emit_ret(std::vector<uint8_t> &code) -> void
    {
        emit_op(code, j1t::vm::opcode::RET);
    }
}

int main()
{
    j1t::vm::program p {};
    {
        std::vector<uint8_t> code;
        emit_push(code, 40u);
        emit_push(code, 2u);
        emit_add(code);
        emit_ret(code);
        p.code = std::move(code);
    }

    j1t::vm::state st_i {};
    st_i.locals.resize(8, 0);
    st_i.stack.clear();
    st_i.memory.clear();

    j1t::vm::state st_j = st_i;

    printf("Running interpreter...\n");
    j1t::vm::interpreter interp {};
    auto                 ri = interp.run(p, st_i);
    if (!ri)
    {
        std::printf(
            "interpreter error: %s\n",
            j1t::vm::interpreter::error_to_string(ri.error())
        );
        return 1;
    }

    printf("Running JIT...\n");
    j1t::jit::engine jit {};
    auto             rj = jit.run(p, st_j);
    if (!rj)
    {
        std::printf(
            "jit error: %s\n",
            j1t::vm::interpreter::error_to_string(rj.error())
        );
        return 1;
    }

    std::printf("interp=%u jit=%u\n", ri->return_value, rj->return_value);
    return (ri->return_value == rj->return_value) ? 0 : 1;
}

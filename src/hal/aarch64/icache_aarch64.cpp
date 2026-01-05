#include <hal/interface/icache.hpp>

namespace j1t::hal
{
    auto flush_instruction_cache(void *begin, uintmax_t size) -> void
    {
        auto *b = static_cast<char *>(begin);
        auto *e = b + size;

        __builtin___clear_cache(b, e);
    }
}

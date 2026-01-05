#include <hal/aarch64/executable_memory_macos.hpp>

#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <pthread.h>
#endif

namespace
{
    auto round_up_to_page_size(uintmax_t size) -> uintmax_t
    {
        auto      page      = ::sysconf(_SC_PAGESIZE);
        uintmax_t page_size = static_cast<uintmax_t>(page > 0 ? page : 4096);
        return (size + (page_size - 1u)) & ~(page_size - 1);
    }
}

namespace j1t::hal::aarch64
{
    executable_memory_macos::executable_memory_macos(uintmax_t size)
        : size_internal(::round_up_to_page_size(size))
    {
        static constexpr auto protect_flag = PROT_READ | PROT_WRITE | PROT_EXEC;
        static constexpr auto flags        = MAP_ANON | MAP_PRIVATE | MAP_JIT;

        void *ptr = ::mmap(NULL, size_internal, protect_flag, flags, -1, 0);
        if (ptr == MAP_FAILED)
        {
            throw std::runtime_error("mmap failed in executable_memory_macos");
        }

        memory_internal = static_cast<uint8_t *>(ptr);
        ::pthread_jit_write_protect_np(false);
    }

    executable_memory_macos::~executable_memory_macos(void)
    {
        if (memory_internal == nullptr)
        {
            return;
        }

        ::pthread_jit_write_protect_np(true);
        ::munmap(memory_internal, size_internal);
    }

    auto executable_memory_macos::data(void) -> uint8_t *
    {
        return memory_internal;
    }

    auto executable_memory_macos::size(void) const -> uintmax_t
    {
        return size_internal;
    }

    auto executable_memory_macos::begin_write(void) -> void
    {
        ::pthread_jit_write_protect_np(false);
    }

    auto executable_memory_macos::end_write(void) -> void
    {
        ::pthread_jit_write_protect_np(true);
    }

    auto executable_memory_macos::finalize(void) -> void
    {
        end_write();
    }
}

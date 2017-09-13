#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

#include "track.h"

// keep track of original functions we are overloading
namespace overloads {
    template <typename Signature, typename T>
    struct base
    {
        Signature original = nullptr;
        void init() noexcept
        {
            auto ret = dlsym(RTLD_NEXT, T::identifier);
            if (!ret) {
                fprintf(stderr, "Could not find %s\n", T::identifier);
                abort();
            }
            original = reinterpret_cast<Signature>(ret);
        }
        template <typename... Args>
        auto operator()(Args... args) const noexcept -> decltype(original(args...))
        {
            return original(args...);
        }
        explicit operator bool() const noexcept
        {
            return original;
        }
    };

    struct malloc_t : public base<decltype(&::malloc), malloc_t>
    {
        static constexpr const char* identifier = "malloc";
    } malloc;

    struct free_t : public base<decltype(&::free), free_t>
    {
        static constexpr const char* identifier = "free";
    } free;

    struct calloc_t : public base<decltype(&::calloc), calloc_t>
    {
        static constexpr const char* identifier = "calloc";
    } calloc;

    /**
     * Dummy implementation for calloc, to get bootstrapped.
     * This is only called at startup and will eventually be replaced by the
     * "proper" calloc implementation.
     */
    void* dummy_calloc(size_t num, size_t size) noexcept
    {
        const size_t MAX_SIZE = 1024;
        static char* buf[MAX_SIZE];
        static size_t offset = 0;
        if (!offset) {
            memset(buf, 0, MAX_SIZE);
        }
        size_t oldOffset = offset;
        offset += num * size;
        if (offset >= MAX_SIZE) {
            fprintf(stderr, "failed to initialize, dummy calloc buf size exhausted: "
                            "%zu requested, %zu available\n",
                    offset, MAX_SIZE);
            abort();
        }
        return buf + oldOffset;
    }

    void init()
    {
        overloads::calloc.original = &dummy_calloc;
        overloads::malloc.init();
        overloads::free.init();
        overloads::calloc.init();

        memory::init();

        // unset to prevent measuring subprocesses
        unsetenv("LD_PRELOAD");
    }
} // end namespace overloads

// the actual overloads happen here, in C to be fully compliant
extern "C" {
    void* malloc(size_t size) noexcept
    {
        if (!overloads::malloc) {
            overloads::init();
        }

        void* ptr = overloads::malloc(size);

        memory::track(ptr,size);

        return ptr;
    }

    void free(void* ptr) noexcept
    {
        if (!overloads::free) {
            overloads::init();
        }

        memory::release(ptr);

        overloads::free(ptr);
    }

    void* calloc(size_t num, size_t size) noexcept
    {
        if (!overloads::calloc) {
            overloads::init();
        }

        void* ptr = overloads::calloc(num, size);

        if (ptr) {
            memory::track(ptr,num*size);
        }

        return ptr;
    }
} // end extern C

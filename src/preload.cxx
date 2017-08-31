#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

// the public parts of the library

namespace memory {
    std::string scope;
    void set_scope(std::string s) {
        scope = s;
    }
}

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

    void init()
    {
        overloads::malloc.init();
        overloads::free.init();
        overloads::calloc.init();
        
        unsetenv("LD_PRELOAD");
    }
}

extern "C" {
    void* malloc(size_t size) noexcept
    {
        if (!overloads::malloc) {
            overloads::init();
        }

        void* ptr = overloads::malloc(size);
        fprintf(stderr, "malloc: %ux, %ul\n", ptr, size);
        return ptr;
    }

    void free(void* ptr) noexcept
    {
        if (!overloads::free) {
            overloads::init();
        }

        fprintf(stderr, "free: %ux\n", ptr);

        overloads::free(ptr);
    }

    void* calloc(size_t num, size_t size) noexcept
    {
        if (!overloads::calloc) {
            overloads::init();
        }

        void* ret = overloads::calloc(num, size);

        if (ret) {
            fprintf(stderr, "calloc: %ux, %ul, %ul\n", ret, num, size);
        }

        return ret;
    }
} // end extern C

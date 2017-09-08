#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <atomic>
#include <mutex>

#include "track.h"

namespace {
    class RecursionGuard
    {
    public:
        RecursionGuard() : recursion(false)
        {
            recursion = recursion_guard_.test_and_set(std::memory_order_acquire);
        }
        ~RecursionGuard() {
            if (!recursion) {
                recursion_guard_.clear(std::memory_order_release);
            }
        }
        bool recursion;
    private:
        static thread_local std::atomic_flag recursion_guard_;
    };
    thread_local std::atomic_flag RecursionGuard::recursion_guard_ = ATOMIC_FLAG_INIT;
    static std::atomic<bool> tracking_enabled(false);

    class Tracking
    {
    public:
        Tracking();
        ~Tracking();

        // non-copyable, non-movable
        Tracking(const Tracking&) = delete;
        Tracking(Tracking&&) = delete;
        Tracking& operator=(const Tracking&) = delete;
        Tracking& operator=(Tracking&&) = delete;

        // add memory at address with scope and size
        void add(void*, std::string, size_t);

        // remove memory at address
        void remove(void*);

        // get current scope map as a copy
        std::unordered_map<std::string, size_t> get_extents();

    private:
        std::unordered_map<std::string, size_t> scope_map_;
        std::unordered_map<void*, std::pair<std::string, size_t>> ptr_map_;
        std::mutex thread_guard_;
    };

    static Tracking map;

    Tracking::Tracking()
    {
        tracking_enabled = true;
    }

    Tracking::~Tracking()
    {
        tracking_enabled = false;
        if (!scope_map_.empty()) {
            std::cout << "Unfreed memory:\n";
            for(auto& pair : scope_map_) {
                std::cout << "  " << pair.first << " - " << pair.second << "\n";
            }
        }
    }

    void
    Tracking::add(void* addr, std::string scope, size_t size)
    {
        std::lock_guard<std::mutex> lock(thread_guard_);
        auto ret = ptr_map_.emplace(std::make_pair(addr, std::make_pair(scope,size)));
        if (!ret.second) {
            std::cerr << "duplicate memory address" << addr << "\n";
        } else {
            auto iter = scope_map_.find(scope);
            if (iter == scope_map_.end()) {
                scope_map_.emplace(std::make_pair(scope,0));
            } else {
                iter->second += size;
            }
        }
    }

    void
    Tracking::remove(void* addr)
    {
        std::lock_guard<std::mutex> lock(thread_guard_);
        auto iter = ptr_map_.find(addr);
        if (iter != ptr_map_.end()) {
            auto scope = iter->second.first;
            auto size = iter->second.second;
            auto iter2 = scope_map_.find(scope);
            if (iter2 != scope_map_.end()) {
                if (iter2->second <= size) {
                    iter2->second = 0;
                } else {
                    iter2->second -= size;
                }
            }
            ptr_map_.erase(iter);
        }
    }

    
    std::unordered_map<std::string, size_t>
    Tracking::get_extents()
    {
        std::lock_guard<std::mutex> lock(thread_guard_);
        
    }
} // end anon namespace

namespace memory {
    std::string scope;
    void set_scope(std::string s)
    {
        RecursionGuard r;
        scope = s;
    }

    void track(void* addr, size_t size)
    {
        RecursionGuard r;
        if (r.recursion or !tracking_enabled)
            return; // no tracking on recursion

        fprintf(stderr, "tracking addr 0x%08x with size %8u bytes in scope %s\n", addr, size, scope.c_str());
        if (!scope.empty()) {
            map.add(addr,scope,size);
        }
    }

    void release(void* addr)
    {
        RecursionGuard r;
        if (r.recursion or !tracking_enabled)
            return; // no tracking on recursion

        fprintf(stderr, "release addr 0x%08x\n", addr);
        map.remove(addr);
    }

} // end namespace memory
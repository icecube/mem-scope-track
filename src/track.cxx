#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>
#include <chrono>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "track.h"

namespace {

    class Tracking;

    // get a randomly named output file in the current directory
    class Outfile
    {
    public:
        Outfile();
        ~Outfile() = default;

        // non-copyable, movable
        Outfile(const Outfile&) = delete;
        Outfile(Outfile&&) = default;
        Outfile& operator=(const Outfile&) = delete;
        Outfile& operator=(Outfile&&) = default;

        template<typename T>
        std::ostream& operator<<(T);

        inline std::string get_filename() const { return filename_; }
    private:
        std::string filename_;
        std::unique_ptr<std::ofstream> outfile_base_;
        std::unique_ptr<boost::iostreams::filtering_ostreambuf> out_filter_;
        std::unique_ptr<std::ostream> outfile_stream_;
    };


    class TrackingThread
    {
    public:
        TrackingThread() = delete;
        TrackingThread(const Tracking&);
        ~TrackingThread();

        // non-copyable, non-movable
        TrackingThread(const TrackingThread&) = delete;
        TrackingThread(TrackingThread&&) = delete;
        TrackingThread& operator=(const TrackingThread&) = delete;
        TrackingThread& operator=(TrackingThread&&) = delete;
    private:
        void run();

        std::atomic<bool> running_;
        const Tracking& tracking_;
        std::unique_ptr<std::thread> tracking_thread_;
        std::condition_variable tracking_thread_cv_;
        std::mutex tracking_thread_cv_mutex_;
    };


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

        // start tracking to file
        void start();

        // stop tracking to file
        void stop();

        // add memory at address with scope and size
        void add(void*, std::string, size_t);

        // remove memory at address
        void remove(void*);

        // get current scope map as a copy
        std::unordered_map<std::string, size_t> get_extents() const;

    private:
        std::unordered_map<std::string, size_t> scope_map_;
        std::unordered_map<void*, std::pair<std::string, size_t>> ptr_map_;
        mutable std::mutex thread_guard_;
        std::unique_ptr<TrackingThread> tracking_thread_;
    };
    static Tracking map;


    /** Implementation **/
    
    Outfile::Outfile()
        : filename_("mem-scope-track.")
    {
        auto randchar = []() -> char
        {
            const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
            const size_t max_index = (sizeof(charset) - 1);
            return charset[ rand() % max_index ];
        };
        ;
        for(unsigned i=0;i<10;i++) {
            filename_.push_back(randchar());
        }
        filename_.append(".gz");
        outfile_base_ = std::make_unique<std::ofstream>(filename_, std::ios::out | std::ios::binary);
        if (!outfile_base_) {
            throw std::runtime_error("cannot open memory statistics output file");
        }
        out_filter_ = std::make_unique<boost::iostreams::filtering_ostreambuf>();
        out_filter_->push(boost::iostreams::gzip_compressor());
        out_filter_->push(*outfile_base_);
        outfile_stream_ = std::make_unique<std::ostream>(out_filter_.get());
    }

    template<typename T>
    std::ostream&
    Outfile::operator<<(T value)
    {
        *outfile_stream_ << value;
        return *outfile_stream_;
    }


    TrackingThread::TrackingThread(const Tracking& t)
        : running_(true), tracking_(t)
    {
        tracking_thread_ = std::make_unique<std::thread>(&TrackingThread::run,this);
    }

    TrackingThread::~TrackingThread()
    {
        running_ = false;
        tracking_thread_cv_.notify_all();
        tracking_thread_->join();
    }

    void TrackingThread::run()
    {
        std::string graph_cmd("python timeline.py ");
        {
            using namespace std::chrono_literals;
            auto start = std::chrono::high_resolution_clock::now();
            std::unique_lock<std::mutex> lock(tracking_thread_cv_mutex_);
            Outfile outfile = Outfile();
            graph_cmd += outfile.get_filename();
            
            auto print = [&](){
                auto extents = tracking_.get_extents();
                auto now = std::chrono::high_resolution_clock::now();
                outfile << "---" << std::chrono::duration_cast<std::chrono::microseconds>(now-start).count() << "\n";
                for(auto& pair: extents) {
                    outfile << pair.first << "|" << pair.second << "\n";
                }
            };
            print();

            while(running_) {
                tracking_thread_cv_.wait_for(lock, 100ms, [&](){return running_==true;});
                print();
            }
        }

        // call into python to make graph
        std::cout << graph_cmd;
        //system(graph_cmd);
    }
    

    Tracking::Tracking()
    {
        tracking_enabled = true;
        start();
    }

    Tracking::~Tracking()
    {
        tracking_enabled = false;
        stop();
        bool empty = true;
        for(auto& pair : scope_map_) {
            if (pair.second != 0) {
                empty = false;
                break;
            }
        }
        if (!empty) {
            std::cout << "Unfreed memory:\n";
            for(auto& pair : scope_map_) {
                if (pair.second != 0) {
                    std::cout << "  " << pair.first << " - " << pair.second << "\n";
                }
            }
        }
    }

    void
    Tracking::start()
    {
        tracking_thread_ = std::make_unique<TrackingThread>(*this);
    }

    void
    Tracking::stop()
    {
        tracking_thread_.reset();
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
                scope_map_.emplace(std::make_pair(scope,size));
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
    Tracking::get_extents() const
    {
        std::lock_guard<std::mutex> lock(thread_guard_);
        std::unordered_map<std::string, size_t> ret(scope_map_);
        return ret;
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

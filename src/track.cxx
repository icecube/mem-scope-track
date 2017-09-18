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
#include <random>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/filesystem.hpp>

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


    // create an output file
    class Outfile
    {
    public:
        Outfile() = delete;
        Outfile(std::string);
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

    // get a randomly named output file in the current directory
    class RandomOutfile : public Outfile
    {
    public:
        RandomOutfile();
        ~RandomOutfile() = default;

        // non-copyable, movable
        RandomOutfile(const RandomOutfile&) = delete;
        RandomOutfile(RandomOutfile&&) = default;
    };

    // log output to none, stdout, stderr, or file
    class Log {
    private:
        enum class DEST { none, stdout, stderr, file };
    public:
        Log() : out_(DEST::none)
        {
            char* filename = std::getenv("MEMSCOPETRACK_LOGFILE");
            if (filename != nullptr) {
                if (strncmp(filename,"stdout",6) == 0) {
                    out_ = DEST::stdout;
                } else if (strncmp(filename,"stderr",6) == 0) {
                    out_ = DEST::stderr;
                } else {
                    logfile_ = std::make_unique<Outfile>(filename);
                    out_ = DEST::file;
                }
            }
        }
        ~Log() {
            // necessary so we don't try to log messages after this point
            tracking_enabled = false;
        }

        template<typename ...Ts>
        void print(Ts... args) {
            if (out_ == DEST::file && logfile_) {
                char buf[1024];
                snprintf(buf, 1024, args...);
                *logfile_ << buf;
            } else if (out_ == DEST::stdout) {
                fprintf(stdout, args...);
            } else if (out_ == DEST::stderr) {
                fprintf(stderr, args...);
            } // else, ignore
        }
    private:
        std::unique_ptr<Outfile> logfile_;
        DEST out_;
    };


    class Tracking;

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


    class Tracking
    {
    public:
        Tracking() = delete;
        Tracking(std::shared_ptr<Log>);
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

        // get library path
        inline std::string get_library_path() const
        { return library_path_; }

    private:
        std::shared_ptr<Log> log_;
        std::string library_path_;
        std::unordered_map<std::string, size_t> scope_map_;
        std::unordered_map<void*, std::pair<std::string, size_t>> ptr_map_;
        mutable std::mutex thread_guard_;
        std::unique_ptr<TrackingThread> tracking_thread_;
    };


    /** Implementation **/

    Outfile::Outfile(std::string path)
        : filename_(path)
    {
        auto has_suffix = [&](const std::string &suffix)
        {
            return filename_.size() >= suffix.size() &&
                   filename_.compare(filename_.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        outfile_base_ = std::make_unique<std::ofstream>(filename_, std::ios::out | std::ios::binary);
        if (!outfile_base_) {
            throw std::runtime_error("cannot open output file");
        }
        if (has_suffix(".gz")) {
            // gzip the file
            out_filter_ = std::make_unique<boost::iostreams::filtering_ostreambuf>();
            out_filter_->push(boost::iostreams::gzip_compressor());
            out_filter_->push(*outfile_base_);
            outfile_stream_ = std::make_unique<std::ostream>(out_filter_.get());
        } else {
            // plain file
            outfile_stream_ = std::move(outfile_base_);
        }
    }

    std::string randstr(unsigned length)
    {
        auto randchar = []() -> char
        {
            const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
            const size_t max_index = (sizeof(charset) - 2);
            std::mt19937 rng (std::random_device{}());
            std::uniform_int_distribution<> dist (0, max_index);
            return charset[ dist(rng) ];
        };

        std::string ret;
        for(unsigned i=0;i<length;i++) {
            ret.push_back(randchar());
        }
        return ret;
    }

    RandomOutfile::RandomOutfile()
        : Outfile("mem-scope-track."+randstr(10)+".gz")
    { }

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
        RecursionGuard r;
        boost::filesystem::path p = boost::filesystem::absolute(tracking_.get_library_path());
        p = p.parent_path();
        p /= "python";
        p /= "timeline.py";
        std::string graph_cmd = "python " + p.string();
        {
            using namespace std::chrono_literals;
            auto start = std::chrono::high_resolution_clock::now();
            std::unique_lock<std::mutex> lock(tracking_thread_cv_mutex_);

            std::unique_ptr<Outfile> outfile;
            char* outfile_name = std::getenv("MEMSCOPETRACK_OUTFILE");
            if (outfile_name == nullptr) {
                outfile = std::make_unique<RandomOutfile>();
            } else {
                outfile = std::make_unique<Outfile>(outfile_name);
            }
            graph_cmd += " " + outfile->get_filename();

            auto print = [&](){
                auto extents = tracking_.get_extents();
                auto now = std::chrono::high_resolution_clock::now();
                *outfile << "---" << std::chrono::duration_cast<std::chrono::microseconds>(now-start).count() << "\n";
                for(auto& pair: extents) {
                    *outfile << pair.first << "|" << pair.second << "\n";
                }
            };
            print();

            while(running_) {
                tracking_thread_cv_.wait_for(lock, 100ms, [&](){return running_==false;});
                print();
            }
        }

        // call into python to make graph
        std::cout << graph_cmd << "\n";
        //system(graph_cmd);
    }
    

    Tracking::Tracking(std::shared_ptr<Log> log)
        : log_(log)
    {
        char* preload = std::getenv("LD_PRELOAD");
        if (preload == nullptr) {
            fprintf(stderr, "failed to initialize preload path\n");
            abort();
        }
        boost::filesystem::path p(preload);
        library_path_ = p.string();

        // start the tracking file
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
        if (!empty && log_) {
            log_->print("Unfreed memory:\n");
            for(auto& pair : scope_map_) {
                if (pair.second != 0) {
                    log_->print("  %s - %u\n", pair.first.c_str(), pair.second);
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
        auto iter = ptr_map_.find(addr);
        if (iter == ptr_map_.end()) {
            ptr_map_.emplace(std::make_pair(addr, std::make_pair(scope,size)));
            auto iter2 = scope_map_.find(scope);
            if (iter2 == scope_map_.end()) {
                scope_map_.emplace(std::make_pair(scope,size));
            } else {
                iter2->second += size;
            }
        } else {
            log_->print("duplicate memory address 0x%08x for %8u bytes in scope %s\n", addr, size, scope.c_str());
            log_->print("    previous allocation:                %8u bytes in scope %s\n", iter->second.second, iter->second.first.c_str());
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
    
    static std::shared_ptr<Log> log;
    static std::unique_ptr<Tracking> map;

    // destroy
    void destroy()
    {
        tracking_enabled = false;
        map.reset();
        log.reset();
    }

    // initialize the library
    void init()
    {
        log = std::make_shared<Log>();
        map = std::make_unique<Tracking>(log);
        tracking_enabled = true;
        std::atexit(destroy);
    }

    void track(void* addr, size_t size)
    {
        RecursionGuard r;
        if (r.recursion or !tracking_enabled)
            return; // no tracking on recursion

        log->print("tracking addr 0x%08x with size %8u bytes in scope %s\n", addr, size, scope.c_str());
        if (!scope.empty()) {
            map->add(addr,scope,size);
        }
    }

    void release(void* addr)
    {
        RecursionGuard r;
        if (r.recursion or !tracking_enabled)
            return; // no tracking on recursion

        log->print("release addr 0x%08x\n", addr);
        map->remove(addr);
    }

} // end namespace memory

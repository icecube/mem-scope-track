# mem-scope-track
Memory tracking using self-assigned "scopes" to group memory requests.

Uses the LD_PRELOAD trick to override malloc/free.  

To set the scope, a library should define a snippet like:

```c++
namespace memory {
    void set_scope(std::string s) { }
}
```

This function is normally a no-op, but will be overridden by
the memory tracker to set the scope for future memory requests.

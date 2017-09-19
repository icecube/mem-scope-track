# mem-scope-track
Memory tracking using self-assigned "scopes" to group memory requests.

Uses the LD_PRELOAD trick to override malloc and friends.

## Checkout and Building

Use git to checkout and cmake to build the library:

```
git clone https://github.com/IceCubeOpenSource/mem-scope-track
cd mem-scope-track
mkdir build
cd build
cmake ..
make
```

## Running

Use the LD_PRELOAD environment variable:

```
$ LD_PRELOAD=mem-scope-track.so my_executable
```

## Setting Scopes

To set the scope, a library should define a snippet like:

```c++
namespace memory {
    void set_scope(std::string s) { }
}
```

This must be done in a shared library, not in the main executable.

This function is normally a no-op, but will be overridden by
the memory tracker to set the scope for future memory requests.

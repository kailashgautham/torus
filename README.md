# torus

Ring buffer implementations in C++23. Started as a straight mutex/condvar
ring buffer, then a lock-free SPSC version to see how far the simple design
was from what atomics can do.

| file | queue | API |
|------|-------|-----|
| `include/torus/naive_ring_buffer.hpp` | blocking, mutex + condition variable | `push` / `pop` |
| `include/torus/lockfree_ring_buffer.hpp` | non-blocking, atomic indices | `try_push` / `try_pop` |

## Building

Needs CMake >= 3.10 and a compiler with C++23 support.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Running the tests

```sh
./build/tests/test
```

Tests cover both queues: empty/full boundaries, FIFO ordering,
wrap-around across the whole ring, a capacity-1 buffer, and a producer/consumer
stress run where the consumer checks values arrive as 0,1,2,... in exact order.
Exits non-zero if anything fails.

Under the thread sanitizer:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan -j
setarch $(uname -m) -R ./build-tsan/tests/test
```

The `setarch -R` is not optional on recent kernels: ASLR randomizes mmap enough
that ThreadSanitizer refuses to start without it.

## Benchmarks

```sh
./build/benchmarks/benchmark
```

10M items through a capacity-1M buffer, one producer thread and one consumer
thread, checksum validated at the end. Numbers from one run on my machine
(Ryzen 9 9900X, gcc 14.2, -O2), they move around a bit between runs:

| implementation | time | throughput | avg latency |
|---|---|---|---|
| naive (mutex)    | 660 ms | ~30M ops/sec  | ~33 ns/op  |
| lock-free (SPSC) | 36 ms  | ~555M ops/sec | ~1.8 ns/op |

Most of the gap is just syscalls. Every naive push/pop grabs the mutex and can
wake or sleep on the condvar, which means futex traffic when threads contend.
The lock-free version is two loads and a store per operation and never enters
the kernel.

### False sharing

First version of the lock-free queue kept both index counters next to each
other, which put them on the same cache line. Producer stores write_idx_,
consumer loads it (and vice versa), so the line ping-ponged between cores.
Padding each counter to its own line with alignas(64) fixed that and roughly
doubled throughput. Easy to verify: print `(uintptr_t)&buf.x / 64` for both
indices before and after.

### Memory ordering

A producer writes the slot, then stores write_idx_ with release. The consumer
loads write_idx_ with acquire before reading the slot, so it can never see a
half-written item. The mirror image protects slots being handed back to the
producer. Each thread's own index is loaded relaxed since it is the only
writer of that variable.

### The full check

Indices are free-running: they increase forever and wrap naturally at 2^32,
with `% capacity_` applied only when indexing into storage. Queue size is then
just `write - read`, so full is `write - read >= capacity` and empty is
`read == write`. Unsigned subtraction survives wraparound as long as fewer
than 2^32 items are in flight, which always holds. An earlier attempt stored
wrapped indices and compared `write + 1 == read`; that misses the boundary
case at `write == capacity - 1` and silently overwrites unread slots.

This only works because there is exactly one producer and one consumer. Two
producers incrementing write_idx_ without a CAS loop would corrupt the queue
immediately. That is the price of the SPSC design, and worth remembering
before reaching for it in real code.

## TODO

- power-of-two capacities, bitmask indexing instead of `%`
- MPMC variant

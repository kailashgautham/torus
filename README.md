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

10M items through a million-slot buffer, one producer thread and one consumer
thread, checksum validated at the end. Numbers from my machine (Ryzen 9 9900X,
gcc 14.2, -O2). The naive numbers are stable across runs; the lock-free ones
are not, for reasons explained below:

| implementation                     | time     | throughput        | avg latency    |
|------------------------------------|----------|-------------------|----------------|
| naive (mutex)                      | 660 ms   | ~30M ops/sec      | ~33 ns/op      |
| lock-free, `%` indexing            | 36 ms    | ~555M ops/sec     | ~1.8 ns/op     |
| lock-free, masked (power-of-two)   | 25-63 ms | ~300-800M ops/sec | ~1.3-3.2 ns/op |

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
with indexing into storage done as `idx & (capacity_ - 1)`. Queue size is then
just `write - read`, so full is `write - read >= capacity` and empty is
`read == write`. Unsigned subtraction survives wraparound as long as fewer
than 2^32 items are in flight, which always holds. An earlier attempt stored
wrapped indices and compared `write + 1 == read`; that misses the boundary
case at `write == capacity - 1` and silently overwrites unread slots.

This only works because there is exactly one producer and one consumer. Two
producers incrementing write_idx_ without a CAS loop would corrupt the queue
immediately. That is the price of the SPSC design, and worth remembering
before reaching for it in real code.

### Power-of-two indexing

Mapping a free-running index onto a slot needs `% capacity_`, and since
capacity_ is a runtime value the compiler has to emit an integer division for
it. Division is one of the slowest instructions on the CPU, and here it sat
directly in the hot path: compute the slot, write the item, publish the index,
all waiting on that one instruction.

If the capacity is always a power of two, modulo becomes a bitwise AND.
Subtracting one from a power of two leaves a mask of ones covering exactly the
low bits of the index, so `idx & (capacity_ - 1)` produces the same remainder
in a single cycle.

The constructor enforces this by rounding up with std::bit_ceil instead of
rejecting anything, so asking for 1000 slots hands you 1024 and prints a
warning on stderr when it happens. A zero capacity throws, because bit_ceil(0)
returns 0 and a queue that can never accept an item is nobody's intention.

With only this change the same binary went from ~555M to ~800M ops/sec at its
best, about 44%. Repeat runs land anywhere between 350M and 800M though: at
these speeds where the scheduler happens to place the spinning threads matters
more than a few saved cycles. The naive numbers don't move like this because
their cost is dominated by kernel time, which doesn't care about thread
placement.

## TODO

- MPMC variant
- pin the benchmark threads (taskset) so the lock-free numbers stop wandering

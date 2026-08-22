# torus

Torus is a ring buffer implementation library, written in C++23. Currently, 
there are two implementations: a naive SPSC ring buffer written with mutexes 
and condition variables, and a lock-free SPSC version to gain significant
performance improvements.

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

The tests cover both ring buffers, testing various conditions such as empty/full boundaries,
FIFO ordering, wrap-around, a capacity-1 buffer, and a producer/consumer stress run where
the consumer checks values arrive in the exact order.

Under the thread sanitizer:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan -j
setarch $(uname -m) -R ./build-tsan/tests/test
```

The `setarch -R` is not optional on recent kernels, as ASLR randomizes mmap enough
that ThreadSanitizer refuses to start without it.

## Benchmarks

```sh
./build/benchmarks/benchmark
```

The benchmark runs 10M items through a capacity-1M buffer, with one producer
and one consumer thread as well as a checksum validated at the end. The numbers
below are from one run on my machine; however, re-runs tend to hover around
the same values.

Specs: (Ryzen 9 9900X, gcc 14.2, -O2)

| implementation                     | time     | throughput        | avg latency    |
|------------------------------------|----------|-------------------|----------------|
| naive (mutex)                      | 660 ms   | ~30M ops/sec      | ~33 ns/op      |
| lock-free, `%` indexing            | 36 ms    | ~555M ops/sec     | ~1.8 ns/op     |
| lock-free, masked (power-of-two)   | 25-63 ms | ~300-800M ops/sec | ~1.3-3.2 ns/op |

The lock-free ring buffer has >= 18x lower latency as well as throughput.

### False sharing

The first version of the lock-free queue kept both index counters next to each other, 
increasing the likelihood of them being on the same cache line. A simple padding
with alignas(64) solved this problem and roughly doubled throughput.

### Power-of-two indexing

The simplest version of mapping the index to a slot in the ring buffer requires
a modulo operation, and since capacity_ is a runtime value the compiler has to emit
an integer division for it. Division is extremely slow, and was being performed
in every single push and pop operation.

A neat performance improvement here is to map the capacity to a power of two.
We can then employ the trick whereby modulo becomes a bitwise AND, an extremely
fast operation for the CPU to perform (single cycle).

One quirk to take note of is that if the user inputs a capacity which isn't a power of two,
the ring buffer rounds up the capacity instead, emitting a warning on stderr. 
Zero capacity is rejected as it does not make sense.

This change alone took the same binary from ~555M ops/sec to ~800M ops/sec on
its best run, a ~44% improvement from replacing a single instruction. That
said, re-runs land anywhere between 350M and 800M ops/sec, since at these
speeds, where the OS schedules the two spinning threads matters more than the
few cycles we saved. The naive numbers don't move around like this, because
most of their runtime is spent in the kernel.

## TODO

- MPSC / SPMC / MPMC variants
- pin the benchmark threads (taskset) so the lock-free numbers stop wandering

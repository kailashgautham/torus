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

## Using it in your own code

Torus never creates threads and never touches scheduling. You create the
producer and consumer threads yourself and hand both of them a reference to
the same buffer:

```cpp
#include <thread>
#include <torus/lockfree_ring_buffer.hpp>

lockfree_ring_buffer<int> buffer(1024);

std::thread producer{[&buffer]()
                     {
                         for (int i = 0; i < 1'000'000; ++i)
                         {
                             while (!buffer.try_push(i))
                             {
                                 std::this_thread::yield();
                             }
                         }
                     }};

std::thread consumer{[&buffer]()
                     {
                         int item;
                         for (int i = 0; i < 1'000'000; ++i)
                         {
                             while (!buffer.try_pop(item))
                             {
                                 std::this_thread::yield();
                             }
                         }
                     }};

producer.join();
consumer.join();
```

If you want predictable performance out of your own pipeline, pin each thread
to its own core, the same way the benchmarks do:

```cpp
#include <sched.h>

void pin_to_core(int core)
{
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(core, &cpus);
    sched_setaffinity(0, sizeof(cpus), &cpus);
}
```

Call it as the first statement inside each thread's body, e.g.
`pin_to_core(2)` for the producer and `pin_to_core(3)` for the consumer. The
call applies to whichever thread executes it, so doing it inside the lambda
is all it takes. Linux only, which the project already assumes elsewhere.

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

The benchmark runs 10M items through a capacity-2^20 buffer, with one producer
and one consumer thread as well as a checksum validated at the end. Both threads
pin themselves to separate cores inside the benchmark itself; leaving thread
placement to the OS used to make the lock-free numbers swing wildly between
runs.

Specs: (Ryzen 9 9900X, gcc 14.2, -O2)

| implementation                     | time    | throughput    | avg latency |
|------------------------------------|---------|---------------|-------------|
| naive (mutex)                      | 630 ms  | ~32M ops/sec  | ~31 ns/op   |
| lock-free, `%` indexing*           | 36 ms   | ~555M ops/sec | ~1.8 ns/op  |
| lock-free, masked (power-of-two)   | 21 ms   | ~950M ops/sec | ~1.05 ns/op |

\* measured before pinning went into the benchmark, so treat that row as
indicative rather than directly comparable.

With both threads pinned, the masked ring buffer is consistently around 30x
faster than the mutex version.

There is also a comparison against `boost::lockfree::spsc_queue` under the
same workload (only built when libboost-dev is installed):

```sh
./build/benchmarks/benchmark_boost
```

Pinned, boost lands at ~1.2B ops/sec against our ~950M, putting the
hand-rolled version within ~1.3x of the production baseline.

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
its best run, a ~44% improvement from replacing a single instruction. Back then
re-runs still landed anywhere between 300M and 800M ops/sec, since at these
speeds, where the OS schedules the two spinning threads matters more than the
few cycles we saved. Pinning the threads inside the benchmark settled that
completely, which is where the ~950M ops/sec in the table comes from.

## TODO

- MPSC / SPMC / MPMC variants

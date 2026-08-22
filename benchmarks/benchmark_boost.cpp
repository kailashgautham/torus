// Compares our lock-free ring buffer against boost::lockfree::spsc_queue,
// the established baseline everyone reaches for. Same workload as benchmark.cpp:
// 10M items through a power-of-two buffer, one producer, one consumer.
#include <chrono>
#include <iostream>
#include <thread>

#include <boost/lockfree/spsc_queue.hpp>

#include <torus/lockfree_ring_buffer.hpp>

#include <sched.h>

static void pin_to_core(int core)
{
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(core, &cpus);
    if (sched_setaffinity(0, sizeof(cpus), &cpus) != 0)
    {
        std::cerr << "warning: could not pin to core " << core << "\n";
    }
}

int main()
{
    std::cout << "Torus vs Boost SPSC\n";
    std::cout << "===================\n\n";

    const uint32_t capacity = 1 << 20;
    const uint32_t num_operations = 10'000'000;

    // producer and consumer live on separate cores for reproducible numbers,
    // pick two distinct physical cores that exist on your machine
    const int producer_core = 2;
    const int consumer_core = 3;

    // Torus lock-free ring buffer
    {
        std::cout << "torus lockfree_ring_buffer<int>\n";
        std::cout << "Operations: " << num_operations << "\n";

        lockfree_ring_buffer<int> buffer(capacity);
        uint64_t checksum = 0;

        auto start = std::chrono::high_resolution_clock::now();

        std::thread producer{[&buffer]()
                             {
                                 pin_to_core(producer_core);
                                 for (int i = 0; i < num_operations; ++i)
                                 {
                                     while (!buffer.try_push(i))
                                     {
                                         std::this_thread::yield();
                                     }
                                 }
                             }};

        std::thread consumer{[&buffer, &checksum]()
                             {
                                 pin_to_core(consumer_core);
                                 int item;
                                 for (int i = 0; i < num_operations; ++i)
                                 {
                                     while (!buffer.try_pop(item))
                                     {
                                         std::this_thread::yield();
                                     }
                                     checksum += item;
                                 }
                             }};

        producer.join();
        consumer.join();

        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        const auto ms = static_cast<long double>(duration.count());
        const long double ops_per_sec = (num_operations * 2.0) / (ms / 1000.0);

        // Validate: sum of 0 to n-1 = n * (n-1) / 2
        const uint64_t expected = static_cast<uint64_t>(num_operations) * (num_operations - 1) / 2;
        const bool valid = (checksum == expected);

        std::cout << "Time: " << ms << " ms\n";
        std::cout << "Throughput: " << static_cast<uint64_t>(ops_per_sec) << " ops/sec\n";
        std::cout << "Avg latency: " << (ms * 1000000.0) / (num_operations * 2.0) << " ns/op\n";
        std::cout << "Validation: " << (valid ? "PASS" : "FAIL") << "\n\n";
    }

    // boost::lockfree::spsc_queue
    {
        std::cout << "boost::lockfree::spsc_queue<int>\n";
        std::cout << "Operations: " << num_operations << "\n";

        boost::lockfree::spsc_queue<int, boost::lockfree::capacity<1 << 20>> buffer;
        uint64_t checksum = 0;

        auto start = std::chrono::high_resolution_clock::now();

        std::thread producer{[&buffer]()
                             {
                                 pin_to_core(producer_core);
                                 for (int i = 0; i < num_operations; ++i)
                                 {
                                     while (!buffer.push(i))
                                     {
                                         std::this_thread::yield();
                                     }
                                 }
                             }};

        std::thread consumer{[&buffer, &checksum]()
                             {
                                 pin_to_core(consumer_core);
                                 int item;
                                 for (int i = 0; i < num_operations; ++i)
                                 {
                                     while (!buffer.pop(item))
                                     {
                                         std::this_thread::yield();
                                     }
                                     checksum += item;
                                 }
                             }};

        producer.join();
        consumer.join();

        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        const auto ms = static_cast<long double>(duration.count());
        const long double ops_per_sec = (num_operations * 2.0) / (ms / 1000.0);

        const uint64_t expected = static_cast<uint64_t>(num_operations) * (num_operations - 1) / 2;
        const bool valid = (checksum == expected);

        std::cout << "Time: " << ms << " ms\n";
        std::cout << "Throughput: " << static_cast<uint64_t>(ops_per_sec) << " ops/sec\n";
        std::cout << "Avg latency: " << (ms * 1000000.0) / (num_operations * 2.0) << " ns/op\n";
        std::cout << "Validation: " << (valid ? "PASS" : "FAIL") << "\n\n";
    }

    return 0;
}

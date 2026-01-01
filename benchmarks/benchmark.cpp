#include <chrono>
#include <iostream>
#include <thread>
#include <torus/naive_ring_buffer.hpp>

int main()
{
    std::cout << "Torus Ring Buffer Benchmarks\n";
    std::cout << "============================\n\n";

    const uint32_t capacity = 1'000'000;
    const uint32_t num_operations = 10'000'000;

    // Naive ring buffer benchmark
    {
        std::cout << "Naive Ring Buffer (mutex-based)\n";
        std::cout << "Operations: " << num_operations << "\n";

        naive_ring_buffer<int> buffer(capacity);
        uint64_t checksum = 0;

        auto start = std::chrono::high_resolution_clock::now();

        std::thread producer{[&buffer]()
            {
                for (int i = 0; i < num_operations; ++i)
                {
                    buffer.push(i);
                }
            }};

        std::thread consumer{[&buffer, &checksum]()
            {
                for (int i = 0; i < num_operations; ++i)
                {
                    checksum += buffer.pop();
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
        std::cout << "Avg latency: " << (ms * 1000000.0) / (num_operations * 2.0)
                  << " ns/op\n";
        std::cout << "Validation: " << (valid ? "PASS" : "FAIL") << " (checksum: " << checksum
                  << ", expected: " << expected << ")\n\n";
    }

    return 0;
}

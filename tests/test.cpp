#include <iostream>
#include <thread>
#include <torus/lockfree_ring_buffer.hpp>
#include <torus/naive_ring_buffer.hpp>

static int checks_run = 0;
static int checks_failed = 0;

static void check(bool ok, const char* what)
{
    ++checks_run;
    if (!ok)
    {
        ++checks_failed;
        std::cout << "FAIL: " << what << "\n";
    }
}

static void test_naive_single_thread()
{
    std::cout << "naive_ring_buffer, single thread\n";

    naive_ring_buffer<int> buf(8);
    check(buf.is_empty(), "starts empty");
    check(buf.get_size() == 0, "size starts at 0");

    for (int i = 0; i < 5; ++i)
    {
        buf.push(i * i);
    }

    check(buf.get_size() == 5, "size tracks pushes");
    check(!buf.is_full(), "not full below capacity");
    check(buf.pop() == 0 && buf.pop() == 1 && buf.pop() == 4 && buf.pop() == 9 && buf.pop() == 16,
          "pops return values in FIFO order");
    check(buf.is_empty(), "empty again after draining");

    // push/pop enough times that the indices have to wrap around
    bool wrapped_ok = true;
    for (int round = 0; round < 5; ++round)
    {
        for (int i = 0; i < 8; ++i)
        {
            buf.push(round * 8 + i);
        }
        wrapped_ok = wrapped_ok && buf.is_full();
        for (int i = 0; i < 8; ++i)
        {
            wrapped_ok = wrapped_ok && buf.pop() == round * 8 + i;
        }
    }
    check(wrapped_ok, "order survives wrap-around");
}

static void test_naive_threads()
{
    std::cout << "\nnaive_ring_buffer, one producer + one consumer\n";

    const int count = 500'000;
    naive_ring_buffer<int> buf(1024);

    std::thread producer{[&buf]()
                         {
                             for (int i = 0; i < count; ++i)
                             {
                                 buf.push(i);
                             }
                         }};

    std::thread consumer{[&buf]()
                         {
                             // has to come back as 0,1,2,... in order
                             int expected = 0;
                             while (expected < count)
                             {
                                 if (buf.pop() != expected++)
                                 {
                                     break;
                                 }
                             }
                             check(expected == count, "received every value in order");
                         }};

    producer.join();
    consumer.join();
}

static void test_lockfree_single_thread()
{
    std::cout << "\nlockfree_ring_buffer, single thread\n";

    lockfree_ring_buffer<int> buf(4);
    int out = -1;

    check(!buf.try_pop(out), "try_pop fails on empty buffer");

    bool pushed_all = true;
    for (int i = 0; i < 4; ++i)
    {
        pushed_all = pushed_all && buf.try_push(i * 3);
    }
    check(pushed_all, "pushes succeed up to capacity");
    check(!buf.try_push(1234), "push past capacity is rejected");

    bool drained_ok = true;
    for (int i = 0; i < 4; ++i)
    {
        drained_ok = drained_ok && buf.try_pop(out) && out == i * 3;
    }
    check(drained_ok, "values come back in FIFO order");
    check(!buf.try_pop(out), "drained buffer refuses to pop");

    lockfree_ring_buffer<int> tiny(1);
    check(tiny.try_push(7), "capacity 1 accepts an item");
    check(!tiny.try_push(8), "capacity 1 rejects a second item");
    check(tiny.try_pop(out) && out == 7, "capacity 1 hands the item back");
}

static void test_lockfree_threads()
{
    std::cout << "\nlockfree_ring_buffer, one producer + one consumer\n";

    const int count = 2'000'000;
    lockfree_ring_buffer<int> buf(1024);

    std::thread producer{[&buf]()
                         {
                             for (int i = 0; i < count; ++i)
                             {
                                 while (!buf.try_push(i))
                                 {
                                     std::this_thread::yield();
                                 }
                             }
                         }};

    std::thread consumer{[&buf]()
                         {
                             int expected = 0;
                             int out;
                             while (expected < count)
                             {
                                 if (buf.try_pop(out))
                                 {
                                     if (out != expected++)
                                     {
                                         break;
                                     }
                                 }
                                 else
                                 {
                                     std::this_thread::yield();
                                 }
                             }
                             check(expected == count, "received every value in order");
                         }};

    producer.join();
    consumer.join();
}

int main()
{
    std::cout << "Torus Ring Buffer Tests\n";
    std::cout << "=======================\n\n";

    test_naive_single_thread();
    test_naive_threads();
    test_lockfree_single_thread();
    test_lockfree_threads();

    std::cout << "\n" << checks_run << " checks run, " << checks_failed << " failed\n";
    return checks_failed == 0 ? 0 : 1;
}

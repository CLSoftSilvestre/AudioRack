#include <core/SpscQueue.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

using audiorack::SpscQueue;

TEST_CASE ("SpscQueue: push/pop preserves FIFO order", "[spsc]")
{
    SpscQueue<int, 8> q;
    int out = 0;

    REQUIRE (! q.pop (out));

    for (int i = 0; i < 5; ++i)
        REQUIRE (q.push (i));

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE (q.pop (out));
        CHECK (out == i);
    }

    REQUIRE (! q.pop (out));
    REQUIRE (q.empty());
}

TEST_CASE ("SpscQueue: usable capacity is Capacity - 1", "[spsc]")
{
    SpscQueue<int, 8> q;

    for (int i = 0; i < 7; ++i)
        REQUIRE (q.push (i));

    REQUIRE (! q.push (99));

    int out = 0;
    REQUIRE (q.pop (out));
    REQUIRE (q.push (99));   // freed one slot
}

TEST_CASE ("SpscQueue: indices wrap correctly", "[spsc]")
{
    SpscQueue<int, 4> q;
    int out = 0;

    // Cycle many times through a tiny buffer to cross the wrap point often.
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE (q.push (i));
        REQUIRE (q.push (i + 1000000));
        REQUIRE (q.pop (out));
        CHECK (out == i);
        REQUIRE (q.pop (out));
        CHECK (out == i + 1000000);
    }
}

TEST_CASE ("SpscQueue: two-thread stress keeps order and loses nothing", "[spsc][threads]")
{
    // Run under TSan via the 'macos-tsan' preset; the acquire/release pairing
    // in SpscQueue is exactly what this test exercises.
    constexpr std::int64_t count = 500000;
    SpscQueue<std::int64_t, 1024> q;

    std::int64_t received = 0;
    std::int64_t checksum = 0;
    bool ordered = true;

    std::thread consumer ([&]
    {
        std::int64_t expected = 0, value = 0;

        while (expected < count)
        {
            if (q.pop (value))
            {
                ordered = ordered && (value == expected);
                checksum += value;
                ++expected;
            }
        }

        received = expected;
    });

    for (std::int64_t i = 0; i < count; ++i)
        while (! q.push (i))
            std::this_thread::yield();

    consumer.join();

    CHECK (received == count);
    CHECK (ordered);
    CHECK (checksum == count * (count - 1) / 2);
}

#include "backend/shm/ShmBackend.hpp"
#include "backend/shm/ShmInputChannel.hpp"
#include "backend/shm/ShmOutputChannel.hpp"
#include "backend/shm/ShmRingBuffer.hpp"
#include "core/config/Configuration.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Proxy::Backend::Shm;

TEST_CASE("ShmRingBuffer: Basic Operations and FIFO Invariants", "[shm]")
{
    const std::string testPath = "/tmp/test_shm_ring_basic.dat";
    std::filesystem::remove(testPath);

    {
        ShmRingBuffer ring(testPath, 8);
        REQUIRE(ring.isValid());
        REQUIRE(ring.empty());
        REQUIRE(ring.size() == 0);

        std::string msg1 = "Hello Kernel Bypass";
        Common::Compat::span<const std::byte> span1(reinterpret_cast<const std::byte*>(msg1.data()), msg1.size());
        REQUIRE(ring.push(span1));
        REQUIRE(ring.size() == 1);
        REQUIRE(!ring.empty());

        std::array<std::byte, 128> rxBuf;
        std::size_t popped = ring.popInto(rxBuf);
        REQUIRE(popped == msg1.size());
        std::string recovered(reinterpret_cast<char*>(rxBuf.data()), popped);
        REQUIRE(recovered == msg1);
        REQUIRE(ring.empty());
        REQUIRE(ring.size() == 0);
    }

    std::filesystem::remove(testPath);
}

TEST_CASE("ShmRingBuffer: Ring Wrap-Around and Backpressure", "[shm]")
{
    const std::string testPath = "/tmp/test_shm_ring_wrap.dat";
    std::filesystem::remove(testPath);

    {
        constexpr std::size_t capacity = 4;
        ShmRingBuffer ring(testPath, capacity);
        REQUIRE(ring.isValid());

        // Fill capacity
        for (std::size_t i = 0; i < capacity; ++i) {
            std::string msg = "Item_" + std::to_string(i);
            Common::Compat::span<const std::byte> span(reinterpret_cast<const std::byte*>(msg.data()), msg.size());
            REQUIRE(ring.push(span));
        }

        REQUIRE(ring.full());
        REQUIRE(ring.size() == capacity);

        // Additional push must be rejected when full
        std::string overflow = "Overflow";
        Common::Compat::span<const std::byte> overflowSpan(reinterpret_cast<const std::byte*>(overflow.data()), overflow.size());
        REQUIRE(!ring.push(overflowSpan));

        // Pop all items and verify FIFO order
        for (std::size_t i = 0; i < capacity; ++i) {
            std::array<std::byte, 64> rxBuf;
            std::size_t bytes = ring.popInto(rxBuf);
            std::string recovered(reinterpret_cast<char*>(rxBuf.data()), bytes);
            REQUIRE(recovered == "Item_" + std::to_string(i));
        }
        REQUIRE(ring.empty());

        // Now push across wrap-around boundary
        for (std::size_t i = 0; i < 20; ++i) {
            std::string msg = "Wrapped_" + std::to_string(i);
            Common::Compat::span<const std::byte> span(reinterpret_cast<const std::byte*>(msg.data()), msg.size());
            REQUIRE(ring.push(span));

            std::array<std::byte, 64> rxBuf;
            std::size_t bytes = ring.popInto(rxBuf);
            std::string recovered(reinterpret_cast<char*>(rxBuf.data()), bytes);
            REQUIRE(recovered == msg);
        }
    }

    std::filesystem::remove(testPath);
}

TEST_CASE("ShmRingBuffer: Vectorized Batch Operations", "[shm]")
{
    const std::string testPath = "/tmp/test_shm_ring_batch.dat";
    std::filesystem::remove(testPath);

    {
        ShmRingBuffer ring(testPath, 16);
        REQUIRE(ring.isValid());

        std::vector<std::string> messages = {
            "Packet 0", "Packet 1", "Packet 2", "Packet 3",
            "Packet 4", "Packet 5", "Packet 6", "Packet 7"
        };

        std::vector<Common::Compat::span<const std::byte>> messageSpans;
        for (const auto& msg : messages) {
            messageSpans.emplace_back(reinterpret_cast<const std::byte*>(msg.data()), msg.size());
        }

        std::size_t pushed = ring.pushBatch(messageSpans);
        REQUIRE(pushed == messages.size());
        REQUIRE(ring.size() == messages.size());

        std::array<std::array<std::byte, 64>, 8> rxBuffers;
        std::array<::Backend::BufferSlot, 8> slots;
        for (std::size_t i = 0; i < 8; ++i) {
            slots[i] = ::Backend::BufferSlot { rxBuffers[i], 0 };
        }

        std::size_t popped = ring.popBatch(slots);
        REQUIRE(popped == messages.size());

        for (std::size_t i = 0; i < popped; ++i) {
            std::string rec(reinterpret_cast<char*>(rxBuffers[i].data()), slots[i].size);
            REQUIRE(rec == messages[i]);
        }
        REQUIRE(ring.empty());
    }

    std::filesystem::remove(testPath);
}

TEST_CASE("ShmInputChannel and ShmOutputChannel: End-to-End Inter-Channel Communication", "[shm]")
{
    const std::string testPath = "/tmp/test_shm_channel_e2e.dat";
    std::filesystem::remove(testPath);

    {
        ShmOutputChannel out("shm_out", testPath, 32);
        ShmInputChannel in("shm_in", testPath, 32);

        REQUIRE(out.isValid());
        REQUIRE(in.isValid());

        // Single message send and receiveInto
        std::string payload = "<trade id=\"42\" symbol=\"AAPL\" qty=\"100\"/>";
        Common::Compat::span<const std::byte> sendSpan(reinterpret_cast<const std::byte*>(payload.data()), payload.size());
        REQUIRE(out.send(sendSpan));

        std::array<std::byte, 256> rxBuf;
        auto receiveResult = in.receiveInto(rxBuf, std::chrono::milliseconds(50));
        REQUIRE(receiveResult.hasValue());
        REQUIRE(receiveResult.value() == payload.size());
        std::string recovered(reinterpret_cast<char*>(rxBuf.data()), receiveResult.value());
        REQUIRE(recovered == payload);

        // Vectorized batch send and receiveBatch
        std::vector<std::string> batchMsgs = { "MsgA", "MsgB", "MsgC", "MsgD" };
        std::vector<Common::Compat::span<const std::byte>> batchSpans;
        for (const auto& m : batchMsgs) {
            batchSpans.emplace_back(reinterpret_cast<const std::byte*>(m.data()), m.size());
        }

        std::size_t sentCount = out.sendBatch(batchSpans);
        REQUIRE(sentCount == batchMsgs.size());

        std::array<std::array<std::byte, 32>, 4> batchDestBufs;
        std::array<::Backend::BufferSlot, 4> slots;
        for (std::size_t i = 0; i < 4; ++i) {
            slots[i] = ::Backend::BufferSlot { batchDestBufs[i], 0 };
        }

        auto batchResult = in.receiveBatch(slots, std::chrono::milliseconds(50));
        REQUIRE(batchResult.hasValue());
        REQUIRE(batchResult.value() == batchMsgs.size());
        for (std::size_t i = 0; i < batchResult.value(); ++i) {
            std::string rec(reinterpret_cast<char*>(batchDestBufs[i].data()), slots[i].size);
            REQUIRE(rec == batchMsgs[i]);
        }
    }

    std::filesystem::remove(testPath);
}

TEST_CASE("ShmBackend: Factory and Dynamic Channel Creation", "[shm]")
{
    const std::string confPath = "/tmp/test_shm_config.conf";
    {
        std::ofstream f(confPath);
        f << "shm.path=/tmp/test_shm_backend_default.dat\n";
        f << "channel.alpha.path=/tmp/test_shm_alpha.dat\n";
        f << "channel.beta.dest_path=/tmp/test_shm_beta.dat\n";
    }

    auto config = Core::Config::Configuration::fromFile(confPath);
    ShmBackend backend;
    backend.configure(config);

    auto inChan = backend.createInputChannel("alpha", config);
    REQUIRE(inChan != nullptr);

    auto outChan = backend.createOutputChannel("beta", config);
    REQUIRE(outChan != nullptr);

    std::filesystem::remove(confPath);
    std::filesystem::remove("/tmp/test_shm_alpha.dat");
    std::filesystem::remove("/tmp/test_shm_beta.dat");
}

#include "backend/shm/ShmBackend.hpp"
#include "backend/shm/ShmInputChannel.hpp"
#include "backend/shm/ShmOutputChannel.hpp"
#include "backend/shm/ShmRingBuffer.hpp"
#include "common/io/UniqueFd.hpp"
#include "common/memory/ByteView.hpp"
#include "core/config/Configuration.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
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
        REQUIRE(ring.push(Common::Memory::ByteView(msg1)));
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
            REQUIRE(ring.push(Common::Memory::ByteView(msg)));
        }

        REQUIRE(ring.full());
        REQUIRE(ring.size() == capacity);

        // Additional push must be rejected when full
        std::string overflow = "Overflow";
        REQUIRE(!ring.push(Common::Memory::ByteView(overflow)));

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
            REQUIRE(ring.push(Common::Memory::ByteView(msg)));

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
            messageSpans.emplace_back(Common::Memory::ByteView(msg));
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

TEST_CASE("ShmRingBuffer restricts file permissions and rejects incompatible mappings", "[shm][hardening]")
{
    const std::string testPath = "/tmp/test_shm_ring_hardening.dat";
    std::filesystem::remove(testPath);

    {
        ShmRingBuffer ring(testPath, 4);
        REQUIRE(ring.isValid());
    }

    struct stat status { };
    REQUIRE(::stat(testPath.c_str(), &status) == 0);
    CHECK((status.st_mode & 0777) == 0600);

    {
        Common::Io::UniqueFd fd(::open(testPath.c_str(), O_RDWR));
        REQUIRE(fd);
        const uint32_t invalidCapacity = 3;
        REQUIRE(::pwrite(
                    fd.get(),
                    &invalidCapacity,
                    sizeof(invalidCapacity),
                    static_cast<off_t>(offsetof(ShmHeader, capacity)))
            == static_cast<ssize_t>(sizeof(invalidCapacity)));
    }

    {
        ShmRingBuffer ring(testPath, 4);
        CHECK_FALSE(ring.isValid());
    }

    std::filesystem::remove(testPath);
}

TEST_CASE("ShmRingBuffer rejects corrupted slot lengths and does not truncate to destination", "[shm][hardening]")
{
    const std::string corruptPath = "/tmp/test_shm_ring_corrupt_slot.dat";
    std::filesystem::remove(corruptPath);

    {
        ShmRingBuffer ring(corruptPath, 4);
        REQUIRE(ring.isValid());
        std::array<std::byte, 8> message { };
        REQUIRE(ring.push(message));

        Common::Io::UniqueFd fd(::open(corruptPath.c_str(), O_RDWR));
        REQUIRE(fd);
        const auto invalidLength = static_cast<uint32_t>(ShmRingBuffer::MaxPayloadBytes + 1);
        const auto lengthOffset = sizeof(ShmHeader) + offsetof(ShmSlot, length);
        REQUIRE(::pwrite(fd.get(), &invalidLength, sizeof(invalidLength), static_cast<off_t>(lengthOffset))
            == static_cast<ssize_t>(sizeof(invalidLength)));

        std::array<std::byte, 16> destination { };
        CHECK(ring.popInto(destination) == 0);
        CHECK_FALSE(ring.isValid());
    }
    std::filesystem::remove(corruptPath);

    const std::string smallBufferPath = "/tmp/test_shm_ring_small_buffer.dat";
    std::filesystem::remove(smallBufferPath);
    {
        ShmRingBuffer ring(smallBufferPath, 4);
        REQUIRE(ring.isValid());
        std::array<std::byte, 16> message { };
        REQUIRE(ring.push(message));

        std::array<std::byte, 8> destination { };
        CHECK(ring.popInto(destination) == 0);
        CHECK(ring.isValid());
        CHECK(ring.empty());
    }
    std::filesystem::remove(smallBufferPath);
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
        REQUIRE(out.send(Common::Memory::ByteView(payload)));

        std::array<std::byte, 256> rxBuf;
        ::Backend::BufferSlot rxSlot { rxBuf, 0 };
        auto receiveResult = in.receiveSlot(rxSlot, std::chrono::milliseconds(50));
        REQUIRE(receiveResult.isOk());
        REQUIRE(receiveResult.unwrap() == payload.size());
        std::string recovered(reinterpret_cast<char*>(rxBuf.data()), rxSlot.size);
        REQUIRE(recovered == payload);

        // Vectorized batch send and receiveBatch
        std::vector<std::string> batchMsgs = { "MsgA", "MsgB", "MsgC", "MsgD" };
        std::vector<Common::Compat::span<const std::byte>> batchSpans;
        for (const auto& m : batchMsgs) {
            batchSpans.emplace_back(Common::Memory::ByteView(m));
        }

        std::size_t sentCount = out.sendBatch(batchSpans);
        REQUIRE(sentCount == batchMsgs.size());

        std::array<std::array<std::byte, 32>, 4> batchDestBufs;
        std::array<::Backend::BufferSlot, 4> slots;
        for (std::size_t i = 0; i < 4; ++i) {
            slots[i] = ::Backend::BufferSlot { batchDestBufs[i], 0 };
        }

        auto batchResult = in.receiveBatch(slots, std::chrono::milliseconds(50));
        REQUIRE(batchResult.isOk());
        REQUIRE(batchResult.unwrap() == batchMsgs.size());
        for (std::size_t i = 0; i < batchResult.unwrap(); ++i) {
            std::string rec(reinterpret_cast<char*>(batchDestBufs[i].data()), slots[i].size);
            REQUIRE(rec == batchMsgs[i]);
        }
    }

    std::filesystem::remove(testPath);
}

TEST_CASE("ShmBackend: Factory and Dynamic Channel Creation", "[shm]")
{
    auto config = Core::Config::Configuration::fromJson(R"({
  "shm": { "path": "/tmp/test_shm_backend_default.dat" },
  "channel": {
    "alpha": { "path": "/tmp/test_shm_alpha.dat" },
    "beta": { "dest_path": "/tmp/test_shm_beta.dat" }
  }
})");

    ShmBackend backend;
    backend.configure(config);

    auto inChan = backend.createInputChannel("alpha", config);
    REQUIRE(inChan != nullptr);

    auto outChan = backend.createOutputChannel("beta", config);
    REQUIRE(outChan != nullptr);

    std::filesystem::remove("/tmp/test_shm_alpha.dat");
    std::filesystem::remove("/tmp/test_shm_beta.dat");
}

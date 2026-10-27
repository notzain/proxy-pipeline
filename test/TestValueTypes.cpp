#include "common/io/UniqueFd.hpp"
#include "common/memory/Borrow.hpp"
#include "common/memory/NotNull.hpp"
#include "common/memory/ObserverPtr.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

TEST_CASE("Common::Io::UniqueFd lifecycle and RAII ownership")
{
    SECTION("Default construction holds invalid descriptor")
    {
        Common::Io::UniqueFd ufd;
        CHECK_FALSE(ufd.isValid());
        CHECK(ufd.get() == -1);
        CHECK_FALSE(static_cast<bool>(ufd));
    }

    SECTION("Taking ownership and auto-closing on destruction")
    {
        int pfd[2];
        REQUIRE(::pipe(pfd) == 0);

        int readFd = pfd[0];
        int writeFd = pfd[1];

        {
            Common::Io::UniqueFd ufd(readFd);
            CHECK(ufd.isValid());
            CHECK(ufd.get() == readFd);
            CHECK(static_cast<bool>(ufd));
        }

        // ufd went out of scope; readFd should be closed
        // Writing to writeFd with closed readFd should fail with EBADF or error when checking fcntl
        int flags = ::fcntl(readFd, F_GETFD);
        CHECK(flags == -1);
        CHECK(errno == EBADF);

        ::close(writeFd);
    }

    SECTION("Move semantics")
    {
        int pfd[2];
        REQUIRE(::pipe(pfd) == 0);

        Common::Io::UniqueFd u1(pfd[0]);
        Common::Io::UniqueFd u2(std::move(u1));

        CHECK_FALSE(u1.isValid());
        CHECK(u1.get() == -1);
        CHECK(u2.isValid());
        CHECK(u2.get() == pfd[0]);

        Common::Io::UniqueFd u3;
        u3 = std::move(u2);
        CHECK_FALSE(u2.isValid());
        CHECK(u3.isValid());
        CHECK(u3.get() == pfd[0]);

        ::close(pfd[1]);
    }

    SECTION("release() and reset()")
    {
        int pfd[2];
        REQUIRE(::pipe(pfd) == 0);

        Common::Io::UniqueFd ufd(pfd[0]);
        int raw = ufd.release();
        CHECK_FALSE(ufd.isValid());
        CHECK(raw == pfd[0]);

        // Manually close the released fd
        ::close(raw);

        ufd.reset(pfd[1]);
        CHECK(ufd.isValid());
        CHECK(ufd.get() == pfd[1]);
        ufd.reset(-1);
        CHECK_FALSE(ufd.isValid());
    }
}

TEST_CASE("Common::Memory::NotNull guarantees and semantics")
{
    int value = 42;

    SECTION("Construct from valid raw pointer")
    {
        Common::Memory::NotNull<int*> notNullPtr(&value);
        CHECK(*notNullPtr == 42);
        *notNullPtr = 99;
        CHECK(value == 99);
    }

    SECTION("Construct from ObserverPtr")
    {
        value = 42;
        Common::Memory::ObserverPtr<int> obs(&value);
        Common::Memory::NotNull<Common::Memory::ObserverPtr<int>> notNullObs(obs);
        CHECK(*notNullObs == 42);
        *notNullObs = 123;
        CHECK(value == 123);
        CHECK(notNullObs.get() == obs);
    }

    SECTION("Implicit conversion to underlying pointer type")
    {
        Common::Memory::NotNull<int*> notNullPtr(&value);
        int* raw = notNullPtr;
        CHECK(raw == &value);
    }

    SECTION("Comparison with raw pointer and ObserverPtr")
    {
        Common::Memory::NotNull<int*> p1(&value);
        Common::Memory::NotNull<int*> p2(&value);
        CHECK(p1 == p2);
        CHECK(p1 == &value);
    }
}

TEST_CASE("Common::Memory::Borrow rebindable reference semantics")
{
    int val1 = 10;
    int val2 = 20;

    SECTION("Construction from lvalue reference and access")
    {
        Common::Memory::Borrow<int> b(val1);
        CHECK(*b == 10);
        CHECK(b.get() == 10);

        *b = 15;
        CHECK(val1 == 15);
    }

    SECTION("Rebinding to another reference")
    {
        Common::Memory::Borrow<int> b(val1);
        CHECK(&b.get() == &val1);

        b = val2;
        CHECK(&b.get() == &val2);
        CHECK(*b == 20);

        *b = 25;
        CHECK(val2 == 25);
        CHECK(val1 == 10); // val1 untouched
    }

    SECTION("Implicit conversion to reference and ObserverPtr")
    {
        Common::Memory::Borrow<int> b(val1);

        int& ref = b;
        CHECK(&ref == &val1);

        Common::Memory::ObserverPtr<int> obs = b;
        CHECK(obs.get() == &val1);
    }

    SECTION("Can be stored in std::vector")
    {
        std::vector<Common::Memory::Borrow<int>> vec;
        vec.emplace_back(val1);
        vec.emplace_back(val2);

        CHECK(vec.size() == 2);
        CHECK(*vec[0] == 10);
        CHECK(*vec[1] == 20);

        *vec[0] = 100;
        CHECK(val1 == 100);
    }
}

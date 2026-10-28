#include <array>
#include <catch2/catch_test_macros.hpp>
#include <common/compat/Span.hpp>
#include <common/memory/ByteView.hpp>
#include <common/memory/MutableByteView.hpp>
#include <common/memory/View.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct SampleHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t flags;
};

void processSpan(Common::Compat::span<const std::byte> span, std::size_t expectedSize)
{
    REQUIRE(span.size() == expectedSize);
}

void processMutableSpan(Common::Compat::span<std::byte> span, std::size_t expectedSize)
{
    REQUIRE(span.size() == expectedSize);
    if (!span.empty()) {
        span[0] = static_cast<std::byte>(0xFF);
    }
}

template <typename T>
void processTypedView(Common::Memory::View<const T> view, std::size_t expectedCount)
{
    REQUIRE(view.size() == expectedCount);
}

} // namespace

TEST_CASE("Common Memory: Generic View<T> operations", "[common][memory][view]")
{
    SECTION("Typed View from std::vector and std::array")
    {
        std::vector<int> numbers = { 10, 20, 30, 40, 50 };
        Common::Memory::View<const int> intView = numbers;
        REQUIRE(intView.size() == 5);
        REQUIRE(intView[0] == 10);
        REQUIRE(intView[4] == 50);

        processTypedView<int>(intView, 5);

        std::array<double, 3> doubles = { 1.5, 2.5, 3.5 };
        Common::Memory::View<const double> dblView = doubles;
        REQUIRE(dblView.size() == 3);
        REQUIRE(dblView[1] == 2.5);
    }

    SECTION("Covariant conversion from View<T> to View<const T>")
    {
        std::vector<int> numbers = { 1, 2, 3 };
        Common::Memory::View<int> mutView = numbers;
        mutView[0] = 99;

        Common::Memory::View<const int> constView = mutView;
        REQUIRE(constView.size() == 3);
        REQUIRE(constView[0] == 99);
    }
}

TEST_CASE("Common Memory: ByteView implicit and explicit conversions", "[common][memory][byteview]")
{
    SECTION("Implicit conversion from std::string and std::string_view")
    {
        std::string str = "Hello Proxy ByteView";
        Common::Memory::ByteView view1 = str;
        REQUIRE(view1.size() == str.size());
        REQUIRE(view1.asStringView() == str);
        REQUIRE(view1.asChar() == str.data());

        // Pass directly to a function expecting Common::Compat::span<const std::byte>
        processSpan(view1, str.size());

        std::string_view sv = "StringView ByteView";
        Common::Memory::ByteView view2 = sv;
        REQUIRE(view2.size() == sv.size());
        REQUIRE(view2.asStringView() == sv);
    }

    SECTION("Implicit conversion from std::vector of non-char types")
    {
        std::vector<std::uint32_t> vec = { 0x11223344, 0x55667788, 0x99AABBCC };
        Common::Memory::ByteView view = vec;
        REQUIRE(view.size() == vec.size() * sizeof(std::uint32_t));
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(vec.data()));

        // Check typed interpretation via .as<T>()
        const auto* firstElem = view.as<std::uint32_t>();
        REQUIRE(firstElem != nullptr);
        REQUIRE(*firstElem == 0x11223344);
    }

    SECTION("Implicit conversion from std::array")
    {
        std::array<double, 4> arr = { 1.1, 2.2, 3.3, 4.4 };
        Common::Memory::ByteView view = arr;
        REQUIRE(view.size() == sizeof(arr));
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(arr.data()));
    }

    SECTION("Conversion from raw pointer and count")
    {
        int numbers[] = { 10, 20, 30, 40 };
        Common::Memory::ByteView view(numbers, 4);
        REQUIRE(view.size() == 4 * sizeof(int));
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(numbers));
        REQUIRE(view.as<int>() != nullptr);
        REQUIRE(*view.as<int>() == 10);
    }

    SECTION("Implicit conversion from fixed-size C array")
    {
        std::uint16_t words[5] = { 1, 2, 3, 4, 5 };
        Common::Memory::ByteView view = words;
        REQUIRE(view.size() == 5 * sizeof(std::uint16_t));
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(words));
    }

    SECTION("Direct conversion from single POD / struct object")
    {
        SampleHeader header { .magic = 0x5058454E, .version = 1, .flags = 0 };
        Common::Memory::ByteView view(header);
        REQUIRE(view.size() == sizeof(SampleHeader));
        REQUIRE(view.as<SampleHeader>() != nullptr);
        REQUIRE(view.as<SampleHeader>()->magic == 0x5058454E);

        std::uint64_t scalar = 0x0102030405060708ULL;
        Common::Memory::ByteView scalarView(scalar);
        REQUIRE(scalarView.size() == sizeof(std::uint64_t));
        REQUIRE(*scalarView.as<std::uint64_t>() == scalar);
    }

    SECTION("Prefix, Suffix and Equality operators")
    {
        std::string full = "VIP: Customer Order #42";
        Common::Memory::ByteView view = full;

        // startsWith and endsWith
        REQUIRE(view.startsWith(Common::Memory::ByteView("VIP:")));
        REQUIRE(view.startsWith("VIP:"));
        REQUIRE(!view.startsWith("STD:"));
        REQUIRE(view.endsWith("#42"));
        REQUIRE(!view.endsWith("#99"));

        // Equality with std::string_view and const char*
        REQUIRE(view == "VIP: Customer Order #42");
        REQUIRE("VIP: Customer Order #42" == view);
        REQUIRE(view != "Other Order");
        REQUIRE("Other Order" != view);

        std::string_view sv = full;
        REQUIRE(view == sv);
        REQUIRE(sv == view);

        Common::Memory::ByteView sameView = full;
        REQUIRE(view == sameView);
    }

    SECTION("Subspan and standard span slicing")
    {
        std::string text = "ABCDEFGH";
        Common::Memory::ByteView view = text;
        auto sub = view.subspan(2, 4);
        REQUIRE(sub.size() == 4);
        REQUIRE(std::string_view(reinterpret_cast<const char*>(sub.data()), sub.size()) == "CDEF");
    }

    SECTION("Direct invocation of processSpan with containers using ByteView")
    {
        std::string payload = "Direct payload test";
        processSpan(Common::Memory::ByteView(payload), payload.size());
    }
}

TEST_CASE("Common Memory: MutableByteView writing and conversion to ByteView", "[common][memory][mutablebyteview]")
{
    SECTION("Construct from std::array and std::vector")
    {
        std::array<std::byte, 64> arr { };
        Common::Memory::MutableByteView view(arr);
        REQUIRE(view.size() == 64);

        processMutableSpan(view, 64);
        REQUIRE(arr[0] == static_cast<std::byte>(0xFF));

        std::vector<char> charVec(128, 'A');
        Common::Memory::MutableByteView vecView(charVec);
        REQUIRE(vecView.size() == 128);
        REQUIRE(vecView.asChar()[0] == 'A');
    }

    SECTION("Construct from single mutable POD object")
    {
        SampleHeader header { .magic = 0, .version = 0, .flags = 0 };
        Common::Memory::MutableByteView mutView(header);
        REQUIRE(mutView.size() == sizeof(SampleHeader));

        auto* ptr = mutView.as<SampleHeader>();
        REQUIRE(ptr != nullptr);
        ptr->magic = 0xAABBCCDD;
        REQUIRE(header.magic == 0xAABBCCDD);
    }

    SECTION("Implicit conversion from MutableByteView to immutable ByteView")
    {
        std::array<char, 8> buffer { 'P', 'R', 'O', 'X', 'Y', '1', '2', '3' };
        Common::Memory::MutableByteView mutView(buffer);

        // Implicit conversion
        Common::Memory::ByteView roView = mutView;
        REQUIRE(roView.size() == 8);
        REQUIRE(roView.asStringView() == "PROXY123");
        REQUIRE(roView == "PROXY123");
    }
}

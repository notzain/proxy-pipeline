#include "OrderValidationHandler.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

class TestMockOutputChannel : public Backend::IOutputChannel {
public:
    bool send(std::span<const std::byte> message) override
    {
        std::string str(reinterpret_cast<const char*>(message.data()), message.size());
        m_sentMessages.push_back(std::move(str));
        return true;
    }

    [[nodiscard]] const std::vector<std::string>& sentMessages() const
    {
        return m_sentMessages;
    }

private:
    std::vector<std::string> m_sentMessages;
};

} // namespace

TEST_CASE("OrderValidationHandler deserialization and domain validation", "[order_validation]")
{
    Proxy::Handler::OrderValidationHandler handler("validated_orders");
    auto mockOut = std::make_unique<TestMockOutputChannel>();
    auto* mockOutPtr = mockOut.get();
    handler.registerOutputChannel("validated_orders", std::move(mockOut));

    SECTION("Valid order passes deserialization and validation")
    {
        std::string validXml = "<order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>";
        std::span<const std::byte> span(reinterpret_cast<const std::byte*>(validXml.data()), validXml.size());

        auto parsed = handler.parseOrder(span);
        REQUIRE(parsed.has_value());
        CHECK(parsed->customer == "Alice Smith");
        CHECK(parsed->item == "Widget-Pro");
        CHECK(parsed->quantity == 42);

        CHECK(handler.validateOrder(*parsed));

        handler.onMessageReceived("input_ch", span);
        REQUIRE(mockOutPtr->sentMessages().size() == 1);
        CHECK(mockOutPtr->sentMessages()[0] == validXml);
    }

    SECTION("Invalid XML syntax fails deserialization")
    {
        std::string malformedXml = "<order><customer>Alice<item></order>";
        std::span<const std::byte> span(reinterpret_cast<const std::byte*>(malformedXml.data()), malformedXml.size());

        auto parsed = handler.parseOrder(span);
        CHECK_FALSE(parsed.has_value());

        handler.onMessageReceived("input_ch", span);
        CHECK(mockOutPtr->sentMessages().empty());
    }

    SECTION("Empty payload is rejected")
    {
        std::span<const std::byte> span { };
        auto parsed = handler.parseOrder(span);
        CHECK_FALSE(parsed.has_value());

        handler.onMessageReceived("input_ch", span);
        CHECK(mockOutPtr->sentMessages().empty());
    }

    SECTION("Customer name validation failures")
    {
        // Too short (< 2 chars)
        Proxy::Model::Order shortCustomer { .customer = "A", .item = "Gadget", .quantity = 5 };
        CHECK_FALSE(handler.validateOrder(shortCustomer));

        // Invalid characters (numbers/symbols not permitted in customer name)
        Proxy::Model::Order invalidCharCustomer { .customer = "Alice123!", .item = "Gadget", .quantity = 5 };
        CHECK_FALSE(handler.validateOrder(invalidCharCustomer));
    }

    SECTION("Item validation failures")
    {
        // Too short (< 2 chars)
        Proxy::Model::Order shortItem { .customer = "Alice", .item = "X", .quantity = 5 };
        CHECK_FALSE(handler.validateOrder(shortItem));

        // Invalid characters
        Proxy::Model::Order invalidCharItem { .customer = "Alice", .item = "Widget@#$", .quantity = 5 };
        CHECK_FALSE(handler.validateOrder(invalidCharItem));
    }

    SECTION("Quantity validation failures")
    {
        // Zero quantity
        Proxy::Model::Order zeroQty { .customer = "Alice", .item = "Gadget", .quantity = 0 };
        CHECK_FALSE(handler.validateOrder(zeroQty));

        // Exceeds max limit (> 10,000)
        Proxy::Model::Order excessiveQty { .customer = "Alice", .item = "Gadget", .quantity = 10001 };
        CHECK_FALSE(handler.validateOrder(excessiveQty));
    }

    SECTION("Handler discards messages that fail domain criteria")
    {
        std::string invalidOrderXml = "<order><customer>A</customer><item>Widget</item><quantity>5</quantity></order>";
        std::span<const std::byte> span(reinterpret_cast<const std::byte*>(invalidOrderXml.data()), invalidOrderXml.size());

        handler.onMessageReceived("input_ch", span);
        CHECK(mockOutPtr->sentMessages().empty());
    }
}

TEST_CASE("OrderValidationHandler Catch2 Benchmarks", "[!benchmark][order_validation]")
{
    Proxy::Handler::OrderValidationHandler handler("bench_out");
    auto mockOut = std::make_unique<TestMockOutputChannel>();
    handler.registerOutputChannel("bench_out", std::move(mockOut));

    std::string validXml = "<order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>";
    std::span<const std::byte> span(reinterpret_cast<const std::byte*>(validXml.data()), validXml.size());

    Proxy::Model::Order preParsedOrder {
        .customer = "Alice Smith",
        .item = "Widget-Pro",
        .quantity = 42
    };

    Proxy::Model::OrderView preParsedOrderView {
        .customer = "Alice Smith",
        .item = "Widget-Pro",
        .quantity = 42
    };

    BENCHMARK("Baseline: Order XML Deserialization (std::string)")
    {
        return handler.parseOrder(span);
    };

    BENCHMARK("Option 1: OrderView XML Deserialization (std::string_view)")
    {
        return handler.parseOrderView(span);
    };

    BENCHMARK("Baseline: Order Domain Validation (std::string)")
    {
        return handler.validateOrder(preParsedOrder);
    };

    BENCHMARK("Option 1: OrderView Domain Validation (std::string_view)")
    {
        return handler.validateOrderView(preParsedOrderView);
    };

    BENCHMARK("Baseline: Order Full Ingress Pipeline")
    {
        handler.onMessageReceived("bench_in", span);
    };
}


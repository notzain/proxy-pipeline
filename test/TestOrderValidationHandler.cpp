#include "OrderValidationHandler.hpp"
#include "core/message/MessageEnvelope.hpp"
#include "core/message/MessageView.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

class TestMockOutputChannel : public Backend::IOutputChannel {
public:
    explicit TestMockOutputChannel(std::string name)
        : Backend::IOutputChannel(std::move(name))
    {
    }

    bool send(Common::Compat::span<const std::byte> message) override
    {
        auto optView = Core::Message::MessageView::from(message);
        std::string str = optView
            ? std::string(reinterpret_cast<const char*>(optView->payload().data()), optView->payload().size())
            : std::string(reinterpret_cast<const char*>(message.data()), message.size());
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
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::OrderValidationHandler handler(backend);
    auto mockOut = std::make_unique<TestMockOutputChannel>("validated_orders");
    auto* mockOutPtr = mockOut.get();
    handler.setOutputChannel(handler.registerOutputChannel(std::move(mockOut)));

    SECTION("Valid order passes deserialization and validation")
    {
        std::string validXml = "<order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>";
        Common::Compat::span<const std::byte> span(reinterpret_cast<const std::byte*>(validXml.data()), validXml.size());

        auto parsed = handler.parseOrder(span);
        REQUIRE(parsed.has_value());
        CHECK(parsed->customer == "Alice Smith");
        CHECK(parsed->item == "Widget-Pro");
        CHECK(parsed->quantity == 42);

        CHECK(handler.validateOrder(*parsed));

        std::string validXmlDirect = "<order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>";
        Common::Compat::span<std::byte> spanDirect(reinterpret_cast<std::byte*>(validXmlDirect.data()), validXmlDirect.size());
        auto parsedDirect = handler.parseOrderDirect(spanDirect);
        REQUIRE(parsedDirect.has_value());
        CHECK(parsedDirect->customer == "Alice Smith");
        CHECK(parsedDirect->item == "Widget-Pro");
        CHECK(parsedDirect->quantity == 42);

        std::string validXmlInPlace = "<order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>";
        Common::Compat::span<std::byte> spanInPlace(reinterpret_cast<std::byte*>(validXmlInPlace.data()), validXmlInPlace.size());
        auto parsedInPlace = handler.parseOrderViewInPlace(spanInPlace);
        REQUIRE(parsedInPlace.has_value());
        CHECK(parsedInPlace->customer == "Alice Smith");
        CHECK(parsedInPlace->item == "Widget-Pro");
        CHECK(parsedInPlace->quantity == 42);

        auto parsedLibXml2 = handler.parseOrderLibXml2(span);
        REQUIRE(parsedLibXml2.has_value());
        CHECK(parsedLibXml2->customer == "Alice Smith");
        CHECK(parsedLibXml2->item == "Widget-Pro");
        CHECK(parsedLibXml2->quantity == 42);

        auto parsedLibXml2Reader = handler.parseOrderLibXml2Reader(span);
        REQUIRE(parsedLibXml2Reader.has_value());
        CHECK(parsedLibXml2Reader->customer == "Alice Smith");
        CHECK(parsedLibXml2Reader->item == "Widget-Pro");
        CHECK(parsedLibXml2Reader->quantity == 42);

        auto parsedLibXml2Reused = handler.parseOrderLibXml2Reused(span);
        REQUIRE(parsedLibXml2Reused.has_value());
        CHECK(parsedLibXml2Reused->customer == "Alice Smith");
        CHECK(parsedLibXml2Reused->item == "Widget-Pro");
        CHECK(parsedLibXml2Reused->quantity == 42);

        auto parsedXerces = handler.parseOrderXerces(span);
        REQUIRE(parsedXerces.has_value());
        CHECK(parsedXerces->customer == "Alice Smith");
        CHECK(parsedXerces->item == "Widget-Pro");
        CHECK(parsedXerces->quantity == 42);

        auto parsedXercesReused = handler.parseOrderXercesReused(span);
        REQUIRE(parsedXercesReused.has_value());
        CHECK(parsedXercesReused->customer == "Alice Smith");
        CHECK(parsedXercesReused->item == "Widget-Pro");
        CHECK(parsedXercesReused->quantity == 42);

        auto parsedXercesSax = handler.parseOrderXercesSax(span);
        REQUIRE(parsedXercesSax.has_value());
        CHECK(parsedXercesSax->customer == "Alice Smith");
        CHECK(parsedXercesSax->item == "Widget-Pro");
        CHECK(parsedXercesSax->quantity == 42);

        auto env = Core::Message::MessageEnvelope::packRaw(span);
        auto view = Core::Message::MessageView::from(env);
        REQUIRE(view.has_value());
        handler.onMessageReceived("input_ch", *view);
        REQUIRE(mockOutPtr->sentMessages().size() == 1);
        CHECK(mockOutPtr->sentMessages()[0] == validXml);
    }

    SECTION("Invalid XML syntax fails deserialization")
    {
        std::string malformedXml = "<order><customer>Alice<item></order>";
        Common::Compat::span<const std::byte> span(reinterpret_cast<const std::byte*>(malformedXml.data()), malformedXml.size());

        auto parsed = handler.parseOrder(span);
        CHECK_FALSE(parsed.has_value());

        auto env = Core::Message::MessageEnvelope::packRaw(span);
        auto view = Core::Message::MessageView::from(env);
        REQUIRE(view.has_value());
        handler.onMessageReceived("input_ch", *view);
        CHECK(mockOutPtr->sentMessages().empty());
    }

    SECTION("Empty payload is rejected")
    {
        Common::Compat::span<const std::byte> span { };
        auto parsed = handler.parseOrder(span);
        CHECK_FALSE(parsed.has_value());

        auto env = Core::Message::MessageEnvelope::packRaw(span);
        auto view = Core::Message::MessageView::from(env);
        REQUIRE(view.has_value());
        handler.onMessageReceived("input_ch", *view);
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

        // Option 2 Fast validation behavior matches
        Proxy::Model::OrderView validView { .customer = "Alice Smith", .item = "Widget-Pro", .quantity = 42 };
        CHECK(handler.validateOrderFast(validView));
        Proxy::Model::OrderView invalidView { .customer = "Alice123!", .item = "Widget", .quantity = 5 };
        CHECK_FALSE(handler.validateOrderFast(invalidView));
    }

    SECTION("Handler discards messages that fail domain criteria")
    {
        std::string invalidOrderXml = "<order><customer>A</customer><item>Widget</item><quantity>5</quantity></order>";
        Common::Compat::span<const std::byte> span(reinterpret_cast<const std::byte*>(invalidOrderXml.data()), invalidOrderXml.size());

        auto env = Core::Message::MessageEnvelope::packRaw(span);
        auto view = Core::Message::MessageView::from(env);
        REQUIRE(view.has_value());
        handler.onMessageReceived("input_ch", *view);
        CHECK(mockOutPtr->sentMessages().empty());

        std::string invalidOrderXmlDirect = invalidOrderXml;
        Common::Compat::span<std::byte> mutableSpan(reinterpret_cast<std::byte*>(invalidOrderXmlDirect.data()), invalidOrderXmlDirect.size());
        handler.onMessageReceivedFast("input_ch", mutableSpan);
        CHECK(mockOutPtr->sentMessages().empty());
    }

    SECTION("Handler forwards messages that pass on onMessageReceivedFast")
    {
        std::string validXml = "<order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>";
        Common::Compat::span<std::byte> span(reinterpret_cast<std::byte*>(validXml.data()), validXml.size());

        handler.onMessageReceivedFast("input_ch", span);
        CHECK(mockOutPtr->sentMessages().size() == 1);
    }
}

TEST_CASE("OrderValidationHandler Catch2 Benchmarks", "[!benchmark][order_validation]")
{
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::OrderValidationHandler handler(backend);
    auto mockOut = std::make_unique<TestMockOutputChannel>("bench_out");
    handler.setOutputChannel(handler.registerOutputChannel(std::move(mockOut)));

    std::string validXml = "<order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>";
    Common::Compat::span<const std::byte> span(reinterpret_cast<const std::byte*>(validXml.data()), validXml.size());

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

    BENCHMARK("Option 1b: OrderView XML Deserialization (In-Place IR Traits)")
    {
        std::string xmlCopy = validXml;
        Common::Compat::span<std::byte> mutableSpan(reinterpret_cast<std::byte*>(xmlCopy.data()), xmlCopy.size());
        return handler.parseOrderViewInPlace(mutableSpan);
    };

    BENCHMARK("Option 3: OrderView Direct DOM XML Deserialization (pugixml direct)")
    {
        std::string xmlCopy = validXml;
        Common::Compat::span<std::byte> mutableSpan(reinterpret_cast<std::byte*>(xmlCopy.data()), xmlCopy.size());
        return handler.parseOrderDirect(mutableSpan);
    };

    BENCHMARK("LibXml2: Order Direct DOM XML Deserialization (libxml2 direct)")
    {
        return handler.parseOrderLibXml2(span);
    };

    BENCHMARK("LibXml2: Order Streaming Cursor XML Deserialization (xmlReader)")
    {
        return handler.parseOrderLibXml2Reader(span);
    };

    BENCHMARK("LibXml2: Order Reused Ctxt XML Deserialization (libxml2 reused)")
    {
        return handler.parseOrderLibXml2Reused(span);
    };

    BENCHMARK("Xerces-C: Order Direct DOM XML Deserialization (xerces-c direct)")
    {
        return handler.parseOrderXerces(span);
    };

    BENCHMARK("Xerces-C: Order Reused Parser XML Deserialization (xerces-c reused)")
    {
        return handler.parseOrderXercesReused(span);
    };

    BENCHMARK("Xerces-C: Order SAX2 Event-Driven XML Deserialization (xerces-c sax2)")
    {
        return handler.parseOrderXercesSax(span);
    };

    BENCHMARK("Baseline: Order Domain Validation (std::string)")
    {
        return handler.validateOrder(preParsedOrder);
    };

    BENCHMARK("Option 1: OrderView Domain Validation (std::string_view)")
    {
        return handler.validateOrderView(preParsedOrderView);
    };

    BENCHMARK("Option 2: OrderView Fast Domain Validation (Bitmap Table)")
    {
        return handler.validateOrderFast(preParsedOrderView);
    };

    BENCHMARK("Baseline: Order Full Ingress Pipeline")
    {
        auto env = Core::Message::MessageEnvelope::packRaw(span);
        auto view = *Core::Message::MessageView::from(env);
        handler.onMessageReceived("bench_in", view);
    };

    BENCHMARK("Option 4: Order Full Ingress Pipeline Fast (Cached Channel & In-Place Direct DOM)")
    {
        std::string xmlCopy = validXml;
        Common::Compat::span<std::byte> mutableSpan(reinterpret_cast<std::byte*>(xmlCopy.data()), xmlCopy.size());
        handler.onMessageReceivedFast("bench_in", mutableSpan);
    };
}

#include "XmlVerificationHandler.hpp"
#include "common/memory/ByteView.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

struct TempSchemaFile {
    std::filesystem::path path;

    explicit TempSchemaFile(const std::string& content)
        : path("/tmp/test_schema.xsd")
    {
        std::ofstream out(path);
        out << content;
    }

    ~TempSchemaFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

const std::string sampleXsd = R"(<?xml version="1.0" encoding="UTF-8"?>
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">
    <xs:element name="note">
        <xs:complexType>
            <xs:sequence>
                <xs:element name="to" type="xs:string"/>
                <xs:element name="from" type="xs:string"/>
                <xs:element name="heading" type="xs:string"/>
                <xs:element name="body" type="xs:string"/>
            </xs:sequence>
        </xs:complexType>
    </xs:element>
</xs:schema>
)";

} // namespace

TEST_CASE("XmlVerificationHandler validates compliant XML and rejects non-compliant XML", "[handler][xml]")
{
    TempSchemaFile schema(sampleXsd);
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::XmlVerificationHandler handler(backend, schema.path);

    REQUIRE(handler.isSchemaLoaded());

    std::string validXml = R"(<?xml version="1.0"?>
<note>
    <to>Alice</to>
    <from>Bob</from>
    <heading>Reminder</heading>
    <body>Don't forget the meeting!</body>
</note>)";

    std::string invalidXml = R"(<?xml version="1.0"?>
<note>
    <to>Alice</to>
    <!-- Missing from, heading, body -->
</note>)";

    std::string malformedXml = "<note><to>Incomplete";

    CHECK(handler.validate(Common::Memory::ByteView(validXml)));
    CHECK_FALSE(handler.validate(Common::Memory::ByteView(invalidXml)));
    CHECK_FALSE(handler.validate(Common::Memory::ByteView(malformedXml)));
    CHECK_FALSE(handler.validate({ }));
}

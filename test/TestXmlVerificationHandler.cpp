#include "XmlVerificationHandler.hpp"
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
    Proxy::Handler::XmlVerificationHandler handler(schema.path, "valid_out");

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

    std::span<const std::byte> validSpan(reinterpret_cast<const std::byte*>(validXml.data()), validXml.size());
    std::span<const std::byte> invalidSpan(reinterpret_cast<const std::byte*>(invalidXml.data()), invalidXml.size());
    std::span<const std::byte> malformedSpan(reinterpret_cast<const std::byte*>(malformedXml.data()), malformedXml.size());

    CHECK(handler.validate(validSpan));
    CHECK_FALSE(handler.validate(invalidSpan));
    CHECK_FALSE(handler.validate(malformedSpan));
    CHECK_FALSE(handler.validate({}));
}


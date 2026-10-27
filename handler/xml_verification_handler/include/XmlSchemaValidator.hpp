#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>
#include <filesystem>

// Forward declarations for libxml2 types to keep concrete C headers out of the class declaration
typedef struct _xmlSchema* xmlSchemaPtr;

namespace Proxy::Handler {

/**
 * @brief Thread-safe XSD schema validator encapsulating libxml2 schema parsing and validation.
 *
 * Employs a thread-local validation context pool to eliminate mutex contention across worker threads.
 */
class XmlSchemaValidator {
public:
    /**
     * @brief Constructs and compiles an XSD schema from a file path.
     *
     * @param schemaPath Path to the .xsd file.
     */
    explicit XmlSchemaValidator(const std::filesystem::path& schemaPath);

    /**
     * @brief Destructor releasing compiled schema resources and freeing parser state.
     */
    ~XmlSchemaValidator();

    XmlSchemaValidator(const XmlSchemaValidator&) = delete;
    XmlSchemaValidator& operator=(const XmlSchemaValidator&) = delete;
    XmlSchemaValidator(XmlSchemaValidator&&) noexcept;
    XmlSchemaValidator& operator=(XmlSchemaValidator&&) noexcept;

    /**
     * @brief Checks if the schema was successfully parsed and compiled.
     *
     * @return true if valid and ready to validate documents, false otherwise.
     */
    [[nodiscard]] bool isLoaded() const noexcept;

    /**
     * @brief Validates an in-memory XML byte buffer against the compiled XSD schema.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return true if valid according to schema rules, false if invalid or malformed.
     */
    [[nodiscard]] bool validate(Common::Compat::span<const std::byte> xmlData) const;

private:
    xmlSchemaPtr m_schema { nullptr };
    bool m_loaded { false };
};

} // namespace Proxy::Handler

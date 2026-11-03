#pragma once

#include "IRouteCondition.hpp"
#include "core/serialization/ir/Value.hpp"
#include <string>

namespace Proxy::Handler {

/**
 * @brief Relational operator for metadata field comparison.
 */
enum class FieldOperator {
    Equal,
    NotEqual,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual
};

/**
 * @brief Generic route condition inspecting any protocol metadata field dynamically via MetadataCatalog.
 */
class MetadataFieldCondition : public IRouteCondition {
public:
    MetadataFieldCondition(std::string fieldName,
        FieldOperator op,
        Core::Serialization::Ir::Value expected);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] const std::string& fieldName() const noexcept;
    [[nodiscard]] FieldOperator op() const noexcept;
    [[nodiscard]] const Core::Serialization::Ir::Value& expected() const noexcept;

private:
    std::string m_fieldName;
    FieldOperator m_op;
    Core::Serialization::Ir::Value m_expected;
};

} // namespace Proxy::Handler


#include "MetadataFieldCondition.hpp"
#include "core/metadata/MetadataCatalog.hpp"

namespace Proxy::Handler {

namespace {

    bool compareValues(const Core::Serialization::Ir::Value& actual,
        FieldOperator op,
        const Core::Serialization::Ir::Value& expected) noexcept
    {
        // Numeric comparison
        if (actual.isNumber() && expected.isNumber()) {
            double actNum = *actual.asDouble();
            double expNum = *expected.asDouble();

            switch (op) {
            case FieldOperator::Equal:
                return actNum == expNum;
            case FieldOperator::NotEqual:
                return actNum != expNum;
            case FieldOperator::LessThan:
                return actNum < expNum;
            case FieldOperator::LessThanOrEqual:
                return actNum <= expNum;
            case FieldOperator::GreaterThan:
                return actNum > expNum;
            case FieldOperator::GreaterThanOrEqual:
                return actNum >= expNum;
            }
        }

        // String comparison
        if (actual.isString() && expected.isString()) {
            auto actStr = actual.asStringView().value_or("");
            auto expStr = expected.asStringView().value_or("");

            switch (op) {
            case FieldOperator::Equal:
                return actStr == expStr;
            case FieldOperator::NotEqual:
                return actStr != expStr;
            case FieldOperator::LessThan:
                return actStr < expStr;
            case FieldOperator::LessThanOrEqual:
                return actStr <= expStr;
            case FieldOperator::GreaterThan:
                return actStr > expStr;
            case FieldOperator::GreaterThanOrEqual:
                return actStr >= expStr;
            }
        }

        // Boolean comparison
        if (actual.isBool() && expected.isBool()) {
            bool actBool = *actual.asBool();
            bool expBool = *expected.asBool();
            if (op == FieldOperator::Equal) {
                return actBool == expBool;
            }
            if (op == FieldOperator::NotEqual) {
                return actBool != expBool;
            }
            return false;
        }

        return false;
    }

} // namespace

MetadataFieldCondition::MetadataFieldCondition(std::string fieldName,
    FieldOperator op,
    Core::Serialization::Ir::Value expected)
    : m_fieldName(std::move(fieldName))
    , m_op(op)
    , m_expected(std::move(expected))
{
}

bool MetadataFieldCondition::matches(const Backend::MessageView& message) const noexcept
{
    auto actual = Core::Metadata::MetadataCatalog::getField(
        message.protocol(), message.metadata(), m_fieldName);
    if (!actual.has_value()) {
        return false;
    }
    return compareValues(*actual, m_op, m_expected);
}

std::unique_ptr<IRouteCondition> MetadataFieldCondition::clone() const
{
    return std::make_unique<MetadataFieldCondition>(*this);
}

const std::string& MetadataFieldCondition::fieldName() const noexcept
{
    return m_fieldName;
}

FieldOperator MetadataFieldCondition::op() const noexcept
{
    return m_op;
}

const Core::Serialization::Ir::Value& MetadataFieldCondition::expected() const noexcept
{
    return m_expected;
}

} // namespace Proxy::Handler

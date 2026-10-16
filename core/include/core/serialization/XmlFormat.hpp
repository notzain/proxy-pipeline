#pragma once

#include "core/serialization/Value.hpp"
#include <optional>
#include <pugixml.hpp>
#include <sstream>
#include <string>
#include <string_view>

namespace Core::Serialization {

struct XmlOptions {
    std::string rootName = "root";
    int indentSpaces = 2;
};

/**
 * @brief XML serialization format policy using pugixml.
 */
class XmlFormat {
public:
    using Options = XmlOptions;

    /**
     * @brief Encodes an intermediate Value to an XML string.
     */
    static std::string encode(const Value& ir, const Options& options = { })
    {
        pugi::xml_document doc;
        pugi::xml_node rootNode = doc.append_child(options.rootName.c_str());
        buildXmlNode(ir, rootNode);

        std::ostringstream oss;
        pugi::xml_writer_stream writer(oss);
        std::string indent(options.indentSpaces > 0 ? options.indentSpaces : 1, ' ');
        doc.save(writer, options.indentSpaces > 0 ? indent.c_str() : "", pugi::format_default | pugi::format_no_declaration);
        return oss.str();
    }

    /**
     * @brief Decodes an XML string into an intermediate Value.
     */
    static std::optional<Value> decode(std::string_view input, const Options& options = { })
    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(input.data(), input.size());
        if (!result) {
            return std::nullopt;
        }

        pugi::xml_node rootNode = options.rootName.empty() ? doc.first_child() : doc.child(options.rootName.c_str());
        if (!rootNode) {
            rootNode = doc.first_child();
        }
        if (!rootNode) {
            return std::nullopt;
        }

        return parseXmlNode(rootNode);
    }

private:
    static void buildXmlNode(const Value& val, pugi::xml_node& node)
    {
        // Add attributes
        for (const auto& [attrName, attrValue] : val.attributes()) {
            node.append_attribute(attrName.c_str()).set_value(attrValue.c_str());
        }

        if (val.isNull()) {
            return;
        }
        if (auto b = val.asBool()) {
            node.text().set(*b ? "true" : "false");
            return;
        }
        if (val.isInt()) {
            node.text().set(std::to_string(*val.asInt()).c_str());
            return;
        }
        if (val.isUInt()) {
            node.text().set(std::to_string(*val.asUInt()).c_str());
            return;
        }
        if (val.isDouble()) {
            node.text().set(std::to_string(*val.asDouble()).c_str());
            return;
        }
        if (auto s = val.asString()) {
            node.text().set(s->c_str());
            return;
        }
        if (auto* arr = val.asArray()) {
            for (const auto& item : *arr) {
                pugi::xml_node itemNode = node.append_child("item");
                buildXmlNode(item, itemNode);
            }
            return;
        }
        if (auto* obj = val.asObject()) {
            for (const auto& [fieldName, fieldValue] : *obj) {
                pugi::xml_node fieldNode = node.append_child(fieldName.c_str());
                buildXmlNode(fieldValue, fieldNode);
            }
            return;
        }
    }

    static Value parseXmlNode(const pugi::xml_node& node)
    {
        Value val;

        // Parse attributes
        for (const auto& attr : node.attributes()) {
            val.setAttribute(attr.name(), attr.value());
        }

        // Count child elements
        std::size_t childCount = 0;
        bool allItems = true;
        for (const auto& child : node.children()) {
            if (child.type() == pugi::node_element) {
                ++childCount;
                if (std::string_view(child.name()) != "item") {
                    allItems = false;
                }
            }
        }

        if (childCount == 0) {
            // Leaf node text content
            std::string text = node.text().as_string();
            // Trim whitespace
            auto start = text.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                // Empty element or attribute-only
                val.raw() = ObjectType { };
                return val;
            }
            auto end = text.find_last_not_of(" \t\r\n");
            std::string trimmed = text.substr(start, end - start + 1);

            // Infer primitive type or store string
            if (trimmed == "true") {
                val.raw() = true;
            } else if (trimmed == "false") {
                val.raw() = false;
            } else {
                char* endPtr = nullptr;
                long long ival = std::strtoll(trimmed.c_str(), &endPtr, 10);
                if (endPtr == trimmed.c_str() + trimmed.size()) {
                    val.raw() = static_cast<std::int64_t>(ival);
                } else {
                    double dval = std::strtod(trimmed.c_str(), &endPtr);
                    if (endPtr == trimmed.c_str() + trimmed.size()) {
                        val.raw() = dval;
                    } else {
                        val.raw() = trimmed;
                    }
                }
            }
            return val;
        }

        if (allItems && childCount > 0) {
            // Array of items
            ArrayType arr;
            arr.reserve(childCount);
            for (const auto& child : node.children()) {
                if (child.type() == pugi::node_element) {
                    arr.push_back(parseXmlNode(child));
                }
            }
            val.raw() = std::move(arr);
            return val;
        }

        // Object with child element fields
        ObjectType obj;
        for (const auto& child : node.children()) {
            if (child.type() == pugi::node_element) {
                obj[child.name()] = parseXmlNode(child);
            }
        }
        val.raw() = std::move(obj);
        return val;
    }
};

} // namespace Core::Serialization

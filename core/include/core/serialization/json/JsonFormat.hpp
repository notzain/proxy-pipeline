#pragma once

#include "core/serialization/ir/Value.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace Core::Serialization::Json {

struct JsonOptions {
    int indent = -1; // -1 for compact, >= 0 for pretty printed
};

/**
 * @brief JSON serialization format policy using nlohmann_json.
 */
class JsonFormat {
public:
    using Options = JsonOptions;

    /**
     * @brief Encodes an intermediate Value to a JSON string.
     *
     * @param ir The intermediate Value to encode.
     * @param options Formatting options.
     * @return Encoded JSON string.
     */
    [[nodiscard]] static std::string encode(const Ir::Value& ir, const Options& options = { });

    /**
     * @brief Decodes a JSON string into an intermediate Value.
     *
     * @param input JSON string or view.
     * @return Deserialized Value if parsing succeeds, std::nullopt otherwise.
     */
    [[nodiscard]] static std::optional<Ir::Value> decode(std::string_view input);
};

} // namespace Core::Serialization::Json

namespace Core::Serialization {
using Json::JsonFormat;
using Json::JsonOptions;
} // namespace Core::Serialization

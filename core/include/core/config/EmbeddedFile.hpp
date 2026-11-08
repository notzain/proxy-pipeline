#pragma once

#include "common/memory/ObserverPtr.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Core::Config {

class Configuration;

/**
 * @brief Represents a configuration resource that can originate either from an on-disk
 * filesystem path or from embedded, inlined string content.
 *
 * Provides a uniform `.content()` accessor that either returns the in-memory content directly
 * or transparently loads the file from the filesystem.
 */
class EmbeddedFile {
public:
    /**
     * @brief Constructs an EmbeddedFile containing raw inlined content.
     *
     * @param content Raw string contents of the file.
     * @return EmbeddedFile Resource backed by in-memory content.
     */
    static EmbeddedFile fromContent(std::string content);

    /**
     * @brief Constructs an EmbeddedFile referencing a path on the filesystem.
     *
     * @param filePath Path to the target file.
     * @return EmbeddedFile Resource backed by disk file.
     */
    static EmbeddedFile fromPath(std::filesystem::path filePath);

    /**
     * @brief Helper that inspects a Configuration store for embedded content or path.
     *
     * Checks in order:
     * 1. `<keyPrefix>_content` (or `<keyPrefix>.content`) -> embedded content
     * 2. `<keyPrefix>_path` (or `<keyPrefix>.path` or `<keyPrefix>`) -> filesystem path
     *
     * @param config The Configuration object.
     * @param keyPrefix Key base (e.g. "xml.xsd").
     * @return std::optional<EmbeddedFile> Populated if either content or path is found.
     */
    static std::optional<EmbeddedFile> fromConfig(const Configuration& config, std::string_view keyPrefix);

    EmbeddedFile() = default;
    ~EmbeddedFile() = default;

    EmbeddedFile(const EmbeddedFile&) = default;
    EmbeddedFile& operator=(const EmbeddedFile&) = default;
    EmbeddedFile(EmbeddedFile&&) noexcept = default;
    EmbeddedFile& operator=(EmbeddedFile&&) noexcept = default;

    /**
     * @brief Returns whether this file resource was provided directly as inlined content.
     */
    [[nodiscard]] bool isEmbedded() const noexcept;

    /**
     * @brief Returns the associated filesystem path, or an empty path if fully embedded.
     */
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    /**
     * @brief Obtains the content of the file.
     *
     * If embedded, returns the internal content string view.
     * If path-based, loads the content from disk into an internal buffer if not already loaded,
     * and returns a string view of the content.
     *
     * @return std::string_view Content of the file. Empty string view on error.
     */
    [[nodiscard]] std::string_view content() const;

    /**
     * @brief Checks if content is non-empty and loaded (or successfully readable from disk).
     */
    [[nodiscard]] bool isValid() const;

private:
    explicit EmbeddedFile(std::string content);
    explicit EmbeddedFile(std::filesystem::path path);

    mutable std::string m_content;
    std::filesystem::path m_path;
    bool m_isEmbedded { false };
    mutable bool m_loaded { false };
};

} // namespace Core::Config

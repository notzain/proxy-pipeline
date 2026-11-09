#pragma once

#include <string_view>

namespace Proxy::Log::Detail {

/**
 * @brief Extracts the file stem from a path string at compile time.
 *
 * Strips all directory components ('/' or '\\') and the file extension (e.g. '.cpp', '.hpp').
 * This ensures absolute build and source paths are never embedded in compiled binaries
 * or emitted into log outputs, preventing CWE-200/CWE-497 path leakage.
 *
 * Examples:
 *   extractStem("/path/to/GatherHandler.cpp") -> "GatherHandler"
 *   extractStem("C:\\source\\MyClass.hpp")   -> "MyClass"
 *   extractStem("Simple.cpp")               -> "Simple"
 *   extractStem("NoExtension")              -> "NoExtension"
 *
 * @param path Full or partial path to extract stem from.
 * @return std::string_view Compile-time subslice representing the stem.
 */
constexpr std::string_view extractStem(std::string_view path) noexcept
{
    if (path.empty()) {
        return "";
    }

    // Find the last directory separator (supporting both Unix '/' and Windows '\\')
    size_t lastSeparator = path.rfind('/');
    const size_t lastBackslash = path.rfind('\\');
    if (lastSeparator == std::string_view::npos || (lastBackslash != std::string_view::npos && lastBackslash > lastSeparator)) {
        lastSeparator = lastBackslash;
    }

    const std::string_view filename = (lastSeparator == std::string_view::npos)
        ? path
        : path.substr(lastSeparator + 1);

    // Strip trailing file extension (e.g. '.cpp', '.hpp', '.c', '.h')
    const size_t dot = filename.rfind('.');
    if (dot != std::string_view::npos && dot > 0) {
        return filename.substr(0, dot);
    }

    return filename;
}

} // namespace Proxy::Log::Detail

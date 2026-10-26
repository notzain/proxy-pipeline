#pragma once

#include <utility>

namespace Core::Io {

/**
 * @brief Move-only RAII file descriptor wrapper ensuring POSIX file and socket handles are safely closed.
 *
 * Encapsulates a standard integer file descriptor. On destruction or reset,
 * if the descriptor is valid (>= 0), `::close()` is automatically called.
 */
class UniqueFd {
public:
    /**
     * @brief Constructs an empty UniqueFd holding an invalid descriptor (-1).
     */
    constexpr UniqueFd() noexcept = default;

    /**
     * @brief Constructs a UniqueFd taking ownership of the provided raw file descriptor.
     *
     * @param fd The raw file descriptor to manage.
     */
    explicit UniqueFd(int fd) noexcept;

    /**
     * @brief Destructor. Closes the managed file descriptor if valid.
     */
    ~UniqueFd() noexcept;

    /**
     * @brief Move constructor. Transfers ownership of the descriptor from @p other.
     */
    UniqueFd(UniqueFd&& other) noexcept;

    /**
     * @brief Move assignment operator. Closes any currently held descriptor and takes ownership from @p other.
     */
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    /**
     * @brief Returns the underlying file descriptor without releasing ownership.
     */
    [[nodiscard]] int get() const noexcept;

    /**
     * @brief Checks if the managed descriptor is valid (>= 0).
     */
    [[nodiscard]] bool isValid() const noexcept;

    /**
     * @brief Boolean conversion operator checking descriptor validity.
     */
    explicit operator bool() const noexcept;

    /**
     * @brief Releases ownership of the file descriptor without closing it.
     *
     * @return The underlying file descriptor.
     */
    int release() noexcept;

    /**
     * @brief Closes the currently held file descriptor (if valid) and takes ownership of @p newFd.
     *
     * @param newFd The new file descriptor to manage (defaults to -1).
     */
    void reset(int newFd = -1) noexcept;

    /**
     * @brief Swaps the managed descriptor with another UniqueFd.
     */
    void swap(UniqueFd& other) noexcept;

    [[nodiscard]] bool operator==(const UniqueFd& other) const noexcept;
    [[nodiscard]] bool operator!=(const UniqueFd& other) const noexcept;

private:
    int m_fd { -1 };
};

/**
 * @brief Swaps two UniqueFd instances.
 */
inline void swap(UniqueFd& lhs, UniqueFd& rhs) noexcept
{
    lhs.swap(rhs);
}

} // namespace Core::Io


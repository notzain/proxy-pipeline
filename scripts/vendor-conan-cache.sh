#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PRESET="${1:-debug}"

if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [cmake-configure-preset]" >&2
    exit 2
fi

case "${PRESET}" in
    debug | release)
        HOST_PROFILE="${ROOT_DIR}/profiles/conan/ubuntu-x86_64-gcc-11"
        ;;
    debug-cpp17 | release-cpp17)
        HOST_PROFILE="${ROOT_DIR}/profiles/conan/ubuntu-x86_64-gcc-11-cpp17"
        ;;
    *)
        echo "ERROR: Unsupported preset '${PRESET}'." >&2
        echo "Supported presets: debug, release, debug-cpp17, release-cpp17." >&2
        exit 2
        ;;
esac

if [ "${PROXY_CONAN_IN_CONTAINER:-}" != "1" ] &&
    {
        [ "$(uname -s)" != "Linux" ] ||
            [ "$(uname -m)" != "x86_64" ] ||
            ! grep -qx 'ID=ubuntu' /etc/os-release ||
            ! grep -qx 'VERSION_ID="22.04"' /etc/os-release
    }; then
    if ! command -v podman >/dev/null 2>&1; then
        echo "ERROR: Podman is required to create the Ubuntu x86_64 bundle from this machine." >&2
        echo "Install Podman, initialize its VM with 'podman machine init', and start it with 'podman machine start'." >&2
        exit 1
    fi

    echo "==> Starting Ubuntu 22.04 x86_64 container to create the GCC 11.4 cache"
    exec podman run --rm \
        --platform linux/amd64 \
        --env PROXY_CONAN_IN_CONTAINER=1 \
        --volume "${ROOT_DIR}:/workspace" \
        --workdir /workspace \
        ubuntu:22.04 \
        bash -c '
            set -euo pipefail
            export DEBIAN_FRONTEND=noninteractive
            apt-get update
            apt-get install -y --no-install-recommends \
                build-essential \
                ca-certificates \
                gcc-11 \
                g++-11 \
                ninja-build \
                python3 \
                python3-pip \
                python3-venv
            python3 -m venv /opt/proxy-venv
            /opt/proxy-venv/bin/pip install --no-cache-dir "conan>=2,<3" "cmake>=4,<5"
            export PATH="/opt/proxy-venv/bin:${PATH}"
            exec /workspace/scripts/vendor-conan-cache.sh "$1"
        ' _ "${PRESET}"
fi

if [ ! -x /usr/bin/gcc-11 ] || [ ! -x /usr/bin/g++-11 ]; then
    echo "ERROR: /usr/bin/gcc-11 and /usr/bin/g++-11 are required." >&2
    exit 1
fi

GCC_VERSION="$(/usr/bin/gcc-11 -dumpfullversion)"
GXX_VERSION="$(/usr/bin/g++-11 -dumpfullversion)"
if [[ "$GCC_VERSION" != 11.4.* ]] || [[ "$GXX_VERSION" != 11.4.* ]]; then
    echo "ERROR: GCC and G++ 11.4 are required (found ${GCC_VERSION} and ${GXX_VERSION})." >&2
    exit 1
fi

echo "==> Exporting in-repository Conan recipes"
for recipe in \
    common \
    log \
    backend/interface \
    backend/file \
    backend/socket \
    backend/uds \
    backend/shm \
    backend/sim; do
    conan export "${ROOT_DIR}/${recipe}"
done

echo "==> Resolving dependencies for Ubuntu x86_64 with GCC 11.4 (${PRESET})"
cmake --preset "${PRESET}" --fresh \
    -DCMAKE_C_COMPILER=/usr/bin/gcc-11 \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
    -DCONAN_HOST_PROFILE="${HOST_PROFILE}" \
    -DCONAN_BUILD_PROFILE="${HOST_PROFILE}"

GRAPH_DIR="${ROOT_DIR}/build/${PRESET}/conan"
shopt -s nullglob
GRAPH_FILES=("${GRAPH_DIR}"/conan-install-graph-*.json)
shopt -u nullglob

if [ "${#GRAPH_FILES[@]}" -ne 1 ]; then
    echo "ERROR: Expected one Conan install graph in ${GRAPH_DIR}; found ${#GRAPH_FILES[@]}." >&2
    echo "Use a single-configuration CMake preset to create the cache bundle." >&2
    exit 1
fi

VENDOR_DIR="${ROOT_DIR}/thirdparty"
PACKAGE_LIST="${VENDOR_DIR}/proxy-conan-packages-${PRESET}.json"
CACHE_ARCHIVE="${VENDOR_DIR}/proxy-conan-cache-${PRESET}.tgz"
mkdir -p "${VENDOR_DIR}"

echo "==> Creating package list from ${GRAPH_FILES[0]}"
conan list --graph="${GRAPH_FILES[0]}" --format=json --out-file="${PACKAGE_LIST}"

echo "==> Saving dependency cache to ${CACHE_ARCHIVE}"
conan cache save --list="${PACKAGE_LIST}" --file="${CACHE_ARCHIVE}"

echo "Cache bundle ready:"
echo "  ${CACHE_ARCHIVE}"
echo "  ${PACKAGE_LIST}"

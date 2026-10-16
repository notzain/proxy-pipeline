#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# build-oci.sh: Universal OCI / Docker Image Builder for Handlers
#
# Usage:
#   ./scripts/build-oci.sh                # Builds OCI images for ALL discovered handlers
#   ./scripts/build-oci.sh <target_name>  # Builds OCI image for a specific target
#                                         # (e.g. ./scripts/build-oci.sh print_handler_file)
#
# Environment variables:
#   IMAGE_PREFIX : Registry or image prefix (default: proxy)
#   IMAGE_TAG    : Image tag (default: latest)
# -----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_PREFIX="${IMAGE_PREFIX:-proxy}"
IMAGE_TAG="${IMAGE_TAG:-latest}"

# Determine which container CLI is available (docker or podman)
if command -v docker >/dev/null 2>&1; then
    CONTAINER_CLI="docker"
elif command -v podman >/dev/null 2>&1; then
    CONTAINER_CLI="podman"
else
    echo "ERROR: Neither 'docker' nor 'podman' was found in PATH." >&2
    echo "Please install Docker Desktop, Podman, or OrbStack to build OCI images." >&2
    exit 1
fi

# Step 1: If building a specific target passed as an argument
if [ $# -ge 1 ]; then
    TARGET="$1"
    TAG_NAME="${IMAGE_PREFIX}/${TARGET}:${IMAGE_TAG}"
    echo "==> Building OCI image for target: ${TARGET} -> ${TAG_NAME}"
    "${CONTAINER_CLI}" build \
        --build-arg TARGET="${TARGET}" \
        -t "${TAG_NAME}" \
        -f "${ROOT_DIR}/Dockerfile" \
        "${ROOT_DIR}"
    echo "==> Successfully created ${TAG_NAME}"
    exit 0
fi

# Step 2: Build the builder stage first so all targets are compiled once
echo "==> Building shared builder stage cache..."
"${CONTAINER_CLI}" build \
    --target builder \
    -t proxy-builder:cache \
    -f "${ROOT_DIR}/Dockerfile" \
    "${ROOT_DIR}"

# Step 3: Extract manifest.json from the builder stage
echo "==> Extracting discovered targets manifest from builder..."
TARGETS_JSON=$("${CONTAINER_CLI}" run --rm proxy-builder:cache cat /workspace/build/manifest.json 2>/dev/null || "${CONTAINER_CLI}" run --rm proxy-builder:cache cat /workspace/build/targets.json)

echo "Discovered targets manifest:"
echo "${TARGETS_JSON}"

# Parse targets array from JSON using python
TARGET_LIST=$(python3 -c "import json; data = json.loads('''${TARGETS_JSON}'''); print(' '.join(data.get('targets', [])))")

# Step 4: Package each target into its dedicated minimal OCI image
for TARGET in ${TARGET_LIST}; do
    TAG_NAME="${IMAGE_PREFIX}/${TARGET}:${IMAGE_TAG}"
    echo ""
    echo "==> Assembling OCI image: ${TAG_NAME} (target: ${TARGET})"
    "${CONTAINER_CLI}" build \
        --build-arg TARGET="${TARGET}" \
        -t "${TAG_NAME}" \
        -f "${ROOT_DIR}/Dockerfile" \
        "${ROOT_DIR}"
    echo "==> Created ${TAG_NAME}"
done

echo ""
echo "================================================================="
echo "All OCI images built successfully:"
for TARGET in ${TARGET_LIST}; do
    echo "  - ${IMAGE_PREFIX}/${TARGET}:${IMAGE_TAG}"
done
echo "================================================================="

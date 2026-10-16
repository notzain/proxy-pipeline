# syntax=docker/dockerfile:1
# -----------------------------------------------------------------------------
# Base Builder Stage: Compiles Linux binaries using Conan and CMake
# -----------------------------------------------------------------------------
FROM gcc:14-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    python3-venv \
    && rm -rf /var/lib/apt/lists/*

# Install Conan 2 into virtualenv
RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --no-cache-dir --upgrade conan
ENV PATH="/opt/venv/bin:$PATH"

WORKDIR /workspace

# Copy dependency manifests first for maximum Docker layer caching
COPY conanfile.txt .
COPY core/conanfile.py core/
COPY backend/socket/conanfile.py backend/socket/
COPY backend/file/conanfile.py backend/file/

# Pre-generate Conan default profile
RUN conan profile detect --force

# Copy complete source code
COPY . .

# Build all targets in Release mode
RUN cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake && \
    cmake --build build

# Strip all generated handler binaries to minimize OCI image size
RUN find build/bin -type f -exec strip -s {} + 2>/dev/null || true

# -----------------------------------------------------------------------------
# Runtime OCI Stage: Packaging a single handler target into a minimal image
# -----------------------------------------------------------------------------
FROM gcr.io/distroless/cc-debian12:nonroot AS runtime

# The target binary name (e.g. print_handler_file, print_handler_socket)
ARG TARGET

WORKDIR /app

# Copy the specific stripped binary selected by TARGET
COPY --from=builder /workspace/build/bin/${TARGET} /app/handler
COPY --from=builder /workspace/proxy.conf /app/proxy.conf

USER nonroot:nonroot

ENTRYPOINT ["/app/handler"]
CMD ["/app/proxy.conf"]

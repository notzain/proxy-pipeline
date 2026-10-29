export CONAN_HOME="$(pwd)/conan_offline_home"

# 1. Export local recipes
conan export common && conan export log && conan export backend/interface && \
conan export backend/file && conan export backend/socket && conan export backend/uds && \
conan export backend/shm && conan export backend/sim && conan export core

# 2. Download the dependencies from ConanCenter into conan_offline_home
# Download the recipes AND source code tarballs:
conan download "spdlog/1.17.0" -r conancenter
conan download "fmt/12.1.0" -r conancenter
conan download "nlohmann_json/3.11.3" -r conancenter
conan download "pugixml/1.14" -r conancenter
conan download "libxml2/2.13.6" -r conancenter
conan download "xerces-c/3.2.4" -r conancenter
conan download "zlib/1.3.1" -r conancenter
conan download "libiconv/1.17" -r conancenter
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class CoreConan(ConanFile):
    name = "core"
    version = "0.1.0"
    package_type = "library"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_pugixml": [True, False],
        "with_xerces": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_pugixml": True,
        "with_xerces": True,
    }

    exports_sources = "CMakeLists.txt", "src/*", "include/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires(
            "backend-interface/0.1.0", transitive_headers=True, transitive_libs=True
        )
        self.requires("common/0.1.0", transitive_headers=True, transitive_libs=True)
        self.requires("proxylog/0.1.0", transitive_headers=True, transitive_libs=True)
        self.requires("fmt/12.1.0", transitive_headers=True, transitive_libs=True)
        self.requires(
            "nlohmann_json/3.11.3", transitive_headers=True, transitive_libs=True
        )
        if self.options.with_pugixml:
            self.requires("pugixml/1.14", transitive_headers=True, transitive_libs=True)
        if self.options.with_xerces:
            self.requires("xerces-c/3.3.0", transitive_headers=True, transitive_libs=True)

    def build_requirements(self):
        self.test_requires("catch2/3.8.0")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["PROXY_ENABLE_PUGIXML"] = bool(self.options.with_pugixml)
        tc.variables["PROXY_ENABLE_XERCES"] = bool(self.options.with_xerces)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["core"]
        self.cpp_info.set_property("cmake_file_name", "core")
        self.cpp_info.set_property("cmake_target_name", "core::core")

import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy


class BackendUdsConan(ConanFile):
    name = "backend-uds"
    version = "0.1.0"
    package_type = "library"

    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

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
        self.requires("backend-interface/0.1.0", transitive_headers=True)
        self.requires("proxylog/0.1.0")
        if self.settings.os == "Linux":
            self.requires("liburing/2.8")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(
            self,
            "*",
            src=os.path.join(self.source_folder, "include"),
            dst=os.path.join(self.package_folder, "include"),
        )
        copy(
            self,
            "*.a",
            src=self.build_folder,
            dst=os.path.join(self.package_folder, "lib"),
            keep_path=False,
        )
        copy(
            self,
            "*.dylib",
            src=self.build_folder,
            dst=os.path.join(self.package_folder, "lib"),
            keep_path=False,
        )
        copy(
            self,
            "*.so",
            src=self.build_folder,
            dst=os.path.join(self.package_folder, "lib"),
            keep_path=False,
        )

    def package_info(self):
        self.cpp_info.libs = ["backend-uds"]
        self.cpp_info.set_property("cmake_file_name", "backend-uds")
        self.cpp_info.set_property(
            "cmake_target_name", "backend-uds::backend-uds"
        )
        self.cpp_info.set_property("proxy_component", "backend")
        self.cpp_info.set_property("backend_name", "uds")

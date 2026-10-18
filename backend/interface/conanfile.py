import os

from conan import ConanFile
from conan.tools.files import copy
from conan.tools.layout import basic_layout


class BackendInterfaceConan(ConanFile):
    name = "backend-interface"
    version = "0.1.0"
    package_type = "header-library"

    exports_sources = "include/*"

    def layout(self):
        basic_layout(self)

    def package(self):
        copy(
            self,
            "*",
            src=os.path.join(self.source_folder, "include"),
            dst=os.path.join(self.package_folder, "include"),
        )

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.set_property("cmake_file_name", "backend-interface")
        self.cpp_info.set_property(
            "cmake_target_name", "backend-interface::backend-interface"
        )

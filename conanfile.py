from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class CarveConan(ConanFile):
    name = "carve"
    version = "0.1"
    settings = "os", "arch", "compiler", "build_type"
    requires = (
        "qt/6.10.1",
        "bgfx/1.129.8930-495",
        "cgal/6.1.1",
    )
    default_options = {
        "qt/*:shared": True,
        "qt/*:with_pq": False,
    }

    def requirements(self) -> None:
        if self.settings.os == "Linux":
            self.requires("wayland/1.24.0", override=True)

    def generate(self) -> None:
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.generate()

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.files import copy, rmdir
import os

class GeometryAppConan(ConanFile):
    name = "sender_model_app"
    version = "0.0.1"
    settings = "os", "compiler", "build_type", "arch"

    default_options = {
        # CMakeLists.txt asks for graphics, window and system - there is no
        # sound and no networking anywhere in this project. Left at their
        # defaults, those two SFML components drag in a whole audio stack
        # (openal-soft, flac, ogg, vorbis) and an unused network module, all
        # of which get built from source because no prebuilt binary matches
        # this compiler.
        #
        # That is not merely slow. openal-soft 1.22.2 does not compile with
        # GCC 15 at all, so a dependency the project never calls into was the
        # only thing standing between this repository and a working build.
        "sfml/*:audio": False,
        "sfml/*:network": False,
    }

    def requirements(self):
        self.requires("gtest/1.13.0")
        # On Linux SFML comes from apt: .devcontainer/Dockerfile installs
        # libsfml-dev, and the CMakeLists asks for the sfml-graphics,
        # sfml-window and sfml-system targets that its SFMLConfig.cmake
        # exports, so the container build has never gone through Conan for it.
        # Taking it from Conan here as well was not a second opinion, it was a
        # second, worse source: the recipe builds SFML from a tarball on
        # www.sfml-dev.org, which is a single origin that a hosted runner
        # cannot always reach - and when it cannot, a project that has all its
        # dependencies installed fails to build for a reason that has nothing
        # to do with the project.
        #
        # Windows has no apt, so the requirement stays there. That is the same
        # split CppMiddleProject7 makes for Boost and GTest.
        if self.settings.os != "Linux":
            self.requires("sfml/2.6.2")
        self.tool_requires("cmake/3.30.0")
    
    def layout(self):
        self.folders.source = "."
        self.folders.build = "build"
        self.folders.generators = "build/generators"
    
    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        
        tc = CMakeToolchain(self)
        tc.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

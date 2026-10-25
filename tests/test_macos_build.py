"""Exercise macOS DXC selection and bundle resource wiring without an Apple SDK."""
from pathlib import Path
import plistlib
import importlib.util
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MacBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / ".review").mkdir(exist_ok=True)

    def test_real_cmake_host_selection(self):
        with tempfile.TemporaryDirectory(prefix="mac-build-", dir=ROOT / ".review") as tmp:
            fixture = Path(tmp) / "directory with spaces"
            for arch in ("arm64", "x64"):
                path = fixture / "bin" / arch / "dxc-macos"
                path.parent.mkdir(parents=True)
                path.touch()
            for host, arch in (("arm64", "arm64"), ("aarch64", "arm64"), ("x86_64", "x64")):
                script = Path(tmp) / "check.cmake"
                script.write_text(f'''include("{(ROOT / 'cmake/MacOSSupport.cmake').as_posix()}")
beetle_macos_shader_compiler(command "{fixture.as_posix()}" "{host}")
list(LENGTH command count)
if (NOT count EQUAL 5)
    message(FATAL_ERROR "Command arguments split incorrectly: ${{command}}")
endif()
list(GET command 4 compiler)
if (NOT compiler STREQUAL "{fixture.as_posix()}/bin/{arch}/dxc-macos")
    message(FATAL_ERROR "Wrong host compiler: ${{compiler}}")
endif()
''')
                subprocess.run(["cmake", "-P", str(script)], check=True, capture_output=True)
            for host in ("powerpc", "arm64"):
                if host == "arm64":
                    (fixture / "bin/arm64/dxc-macos").unlink()
                script.write_text(f'include("{(ROOT / "cmake/MacOSSupport.cmake").as_posix()}")\n'
                                  f'beetle_macos_shader_compiler(command "{fixture.as_posix()}" "{host}")\n')
                result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Unsupported" if host == "powerpc" else "Missing pinned", result.stderr)

    def test_explicit_deployment_target(self):
        with tempfile.TemporaryDirectory(prefix="mac-target-", dir=ROOT / ".review") as tmp:
            script = Path(tmp) / "check.cmake"
            for version, expected in (("13.0", True), ("14.0", True), ("15.2.1", True),
                                      ("26.0", True), ("12.7", False), ("10.15", False),
                                      ("", False), ("newest", False), ("13.0junk", False)):
                script.write_text(f'include("{(ROOT / "cmake/MacOSSupport.cmake").as_posix()}")\n'
                                  f'beetle_validate_macos_target("{version}")\n')
                result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, expected, result.stderr)

    def test_package_dependency_boundaries(self):
        spec = importlib.util.spec_from_file_location("mac_package", ROOT / "scripts/package-macos.py")
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        deps = package.parse_dependencies("app:\n\t@executable_path/../Frameworks/lib SDL.dylib (compatibility version 1.0.0, current version 1.0.0)\n")
        self.assertEqual(deps, ["@executable_path/../Frameworks/lib SDL.dylib"])
        self.assertEqual(package.parse_minimum_macos("cmd LC_BUILD_VERSION\ncmdsize 32\nplatform 1\nminos 13.3\nsdk 15.0\n"), (13, 3, 0))
        self.assertEqual(package.parse_minimum_macos("cmd LC_VERSION_MIN_MACOSX\ncmdsize 16\nversion 11.0\nsdk 14.0\n"), (11, 0, 0))
        with self.assertRaises(ValueError):
            package.parse_minimum_macos("cmd LC_UUID\n")
        for os_version in ('13.0', '14.0', '15.0', '26.0'):
            baseline = package.validate_baseline(os_version)
            package.require_compatible_minimum('old SDL', (11, 0, 0), baseline)
            package.require_compatible_minimum('app', baseline, baseline)
            with self.assertRaisesRegex(ValueError, 'rebuild this dependency'):
                package.require_compatible_minimum('new SDL', (baseline[0] + 1, 0, 0), baseline)
        with self.assertRaises(ValueError):
            package.validate_baseline('12.7')
        for command in ('cmd LC_BUILD_VERSION\nplatform 6\nminos 13.0\n',
                        'cmd LC_BUILD_VERSION\nplatform 2\nminos 13.0\n',
                        'cmd LC_BUILD_VERSION\nplatform 1\nsdk 15.0\n',
                        'cmd LC_BUILD_VERSION\nplatform 1\nminos 13.0junk\n'):
            with self.assertRaises(ValueError):
                package.parse_minimum_macos(command)
        self.assertEqual(package.parse_minimum_macos('cmd LC_BUILD_VERSION\nplatform macos\nminos 13.0\nsdk 26.0\n'), (13, 0, 0))
        self.assertEqual(package.parse_minimum_macos(
            'fat.dylib (architecture arm64):\nLoad command 1\ncmd LC_BUILD_VERSION\nplatform 1\nminos 13.0\nsdk 26.0\n'
            'Load command 2\ncmd LC_UUID\ncmdsize 24\nuuid placeholder\n'
            'Load command 3\ncmd LC_VERSION_MIN_MACOSX\nversion 14.2\nsdk 26.0\n'), (14, 2, 0))


        self.assertEqual(package.parse_rpaths("Load command 1\n          cmd LC_RPATH\n      cmdsize 40\n         path @loader_path/../Frameworks (offset 12)\n"), ["@loader_path/../Frameworks"])
        with tempfile.TemporaryDirectory(prefix="mac-package-", dir=ROOT / ".review") as tmp:
            app = Path(tmp) / "Application with spaces.app"
            image = app / "Contents/MacOS/BeetleRecomp"
            dylib = app / "Contents/Frameworks/lib SDL.dylib"
            dylib.parent.mkdir(parents=True)
            image.parent.mkdir(parents=True)
            image.touch()
            dylib.touch()
            for path, rpaths in ((deps[0], []), ("@loader_path/../Frameworks/lib SDL.dylib", []),
                                 ("@rpath/lib SDL.dylib", ["@loader_path/../Frameworks"])):
                self.assertEqual(package.resolve_dependency(path, image, app, rpaths), dylib.resolve())
            self.assertIsNone(package.resolve_dependency("/usr/lib/libSystem.B.dylib", image, app, []))
            for path in ("/opt/homebrew/lib/libSDL2.dylib", "@rpath/missing.dylib", "@executable_path/../../../../outside.dylib"):
                with self.assertRaises(ValueError):
                    package.resolve_dependency(path, image, app, [])
            assets = app / "Contents/Resources/assets"
            assets.mkdir(parents=True)
            for name in ("recomp.rcss", *package.REQUIRED_FONTS):
                (assets / name).touch()
            package.inspect_assets(assets)
            (assets / "controls.json").write_text("{}")
            with self.assertRaises(ValueError):
                package.inspect_assets(assets)

    def test_bundle_contract(self):
        plist = plistlib.loads((ROOT / "cmake/BeetleRecomp-Info.plist.in").read_bytes())
        self.assertEqual(plist["CFBundlePackageType"], "APPL")
        self.assertTrue(plist["NSHighResolutionCapable"])
        self.assertEqual(plist["LSMinimumSystemVersion"], "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('MACOSX_BUNDLE TRUE', cmake)
        self.assertIn('$<TARGET_BUNDLE_CONTENT_DIR:BeetleRecomp>/Resources/assets', cmake)
        self.assertIn('enable_language(OBJC OBJCXX)', cmake)
        self.assertIn('target_compile_definitions(plume PRIVATE PLUME_APPLE_RETINA_ENABLED)', cmake)
        self.assertIn('beetle_macos_shader_compiler(DXC', cmake)

if __name__ == "__main__":
    unittest.main()

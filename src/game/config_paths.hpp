#pragma once

#include <filesystem>

namespace bar::config::paths {
// Accept only the standard Foo.app/Contents/MacOS layout; a loose executable
// must not accidentally discover a portable marker several directories above it.
inline std::filesystem::path macos_bundle_parent(const std::filesystem::path& executable_directory) {
    const auto contents = executable_directory.parent_path();
    const auto bundle = contents.parent_path();
    if (executable_directory.filename() == "MacOS" && contents.filename() == "Contents" &&
        bundle.extension() == ".app") return bundle.parent_path();
    return {};
}

// New Mac installs use the normal per-user application data directory. Keep
// using an existing pre-native directory so an upgrade never hides saves or
// rewrites them into a second location. The predicate keeps this policy testable.
template <typename DirectoryExists>
std::filesystem::path macos_directory(const std::filesystem::path& home,
                                     const std::filesystem::path& xdg,
                                     DirectoryExists exists) {
    if (home.empty()) return xdg.empty() ? std::filesystem::path{} : xdg / "BeetleRecomp";
    const auto native = home / "Library" / "Application Support" / "BeetleRecomp";
    if (exists(native)) return native;
    if (!xdg.empty() && exists(xdg / "BeetleRecomp")) return xdg / "BeetleRecomp";
    const auto legacy = home / ".config" / "BeetleRecomp";
    return exists(legacy) ? legacy : native;
}
} // namespace bar::config::paths

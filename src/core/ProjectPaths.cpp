#include "ProjectPaths.h"

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>

namespace outer_wilds {
namespace {

std::filesystem::path g_projectRoot;

std::optional<std::filesystem::path> GetConfiguredRoot() {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "OUTERWILDS_ROOT") != 0 || value == nullptr) {
        return std::nullopt;
    }

    const std::filesystem::path result(value);
    std::free(value);
    return result;
#else
    if (const char* value = std::getenv("OUTERWILDS_ROOT")) {
        return std::filesystem::path(value);
    }
    return std::nullopt;
#endif
}

bool IsProjectRoot(const std::filesystem::path& candidate) {
    return std::filesystem::is_directory(candidate / "assets") &&
           std::filesystem::is_directory(candidate / "shaders");
}

std::filesystem::path SearchParents(std::filesystem::path candidate) {
    std::error_code error;
    candidate = std::filesystem::absolute(candidate, error);
    if (error) {
        return {};
    }

    for (int depth = 0; depth < 8; ++depth) {
        if (IsProjectRoot(candidate)) {
            return std::filesystem::weakly_canonical(candidate, error);
        }

        const auto parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) {
            break;
        }
        candidate = parent;
    }

    return {};
}

} // namespace

void ProjectPaths::Initialize(const char* executablePath) {
    g_projectRoot = FindProjectRoot(executablePath);
    if (g_projectRoot.empty()) {
        throw std::runtime_error(
            "Unable to locate the project root. Set OUTERWILDS_ROOT to a directory containing assets and shaders.");
    }
}

const std::filesystem::path& ProjectPaths::Root() {
    if (g_projectRoot.empty()) {
        Initialize();
    }
    return g_projectRoot;
}

std::filesystem::path ProjectPaths::Asset(std::string_view relativePath) {
    return Root() / "assets" / std::filesystem::path(relativePath);
}

std::filesystem::path ProjectPaths::Shader(std::string_view relativePath) {
    return Root() / "shaders" / std::filesystem::path(relativePath);
}

std::filesystem::path ProjectPaths::FindProjectRoot(const char* executablePath) {
    if (const auto configuredRoot = GetConfiguredRoot()) {
        const auto root = SearchParents(*configuredRoot);
        if (!root.empty()) {
            return root;
        }
    }

    std::vector<std::filesystem::path> candidates;
    std::error_code error;
    candidates.push_back(std::filesystem::current_path(error));

    if (executablePath != nullptr && executablePath[0] != '\0') {
        candidates.push_back(std::filesystem::path(executablePath).parent_path());
    }

    for (const auto& candidate : candidates) {
        const auto root = SearchParents(candidate);
        if (!root.empty()) {
            return root;
        }
    }

    return {};
}

} // namespace outer_wilds

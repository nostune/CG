#pragma once

#include <filesystem>
#include <string_view>

namespace outer_wilds {

class ProjectPaths {
public:
    static void Initialize(const char* executablePath = nullptr);

    static const std::filesystem::path& Root();
    static std::filesystem::path Asset(std::string_view relativePath);
    static std::filesystem::path Shader(std::string_view relativePath);

private:
    static std::filesystem::path FindProjectRoot(const char* executablePath);
};

} // namespace outer_wilds

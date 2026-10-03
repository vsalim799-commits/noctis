#include "Noctis/Core/FileSystem.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace noctis
{
namespace fsimpl
{
class StdFileSystem final : public FileSystem
{
public:
    bool readText(const std::string& path, std::string& out) const override
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    bool writeText(const std::string& path, const std::string& content) const override
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out << content;
        return static_cast<bool>(out);
    }

    bool writeBinary(const std::string& path, const std::vector<u8>& bytes) const override
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(out);
    }

    std::vector<std::string> listFiles(const std::string& directory, const std::string& extension, bool recursive) const override
    {
        std::vector<std::string> files;
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec))
        {
            return files;
        }
        auto consider = [&](const std::filesystem::directory_entry& entry) {
            if (entry.is_regular_file(ec) && entry.path().extension().string() == extension)
            {
                files.push_back(entry.path().generic_string());
            }
        };
        if (recursive)
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec))
            {
                consider(entry);
            }
        }
        else
        {
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
            {
                consider(entry);
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    bool exists(const std::string& path) const override
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    bool makeDirectories(const std::string& path) const override
    {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return !ec;
    }
};
} // namespace fsimpl

std::unique_ptr<FileSystem> makeStdFileSystem() { return std::make_unique<fsimpl::StdFileSystem>(); }

std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty())
    {
        return b;
    }
    if (a.back() == '/' || a.back() == '\\')
    {
        return a + b;
    }
    return a + "/" + b;
}
} // namespace noctis

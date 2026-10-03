// File access abstraction: the headless build uses std::filesystem, the Unreal module plugs in
// IFileManager/FFileHelper so data files work from staged/packaged builds.
#pragma once

#include "Noctis/Core/Platform.h"

#include <memory>
#include <string>
#include <vector>

namespace noctis
{
class NOCTIS_API FileSystem
{
public:
    virtual ~FileSystem() = default;
    virtual bool readText(const std::string& path, std::string& out) const = 0;
    virtual bool writeText(const std::string& path, const std::string& content) const = 0;
    virtual bool writeBinary(const std::string& path, const std::vector<u8>& bytes) const = 0;
    // Lists files (full paths) with the given extension (".json"), optionally recursive. Sorted.
    virtual std::vector<std::string> listFiles(const std::string& directory, const std::string& extension, bool recursive) const = 0;
    virtual bool exists(const std::string& path) const = 0;
    virtual bool makeDirectories(const std::string& path) const = 0;
};

NOCTIS_API std::unique_ptr<FileSystem> makeStdFileSystem();
NOCTIS_API std::string joinPath(const std::string& a, const std::string& b);
} // namespace noctis

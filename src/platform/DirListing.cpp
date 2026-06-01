#include "DirListing.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    std::string ToLower(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Case-insensitive name ordering for the listing.
    bool NameLess(const DirEntry& a, const DirEntry& b)
    {
        return ToLower(a.name) < ToLower(b.name);
    }

    bool IsRootPath(const fs::path& p)
    {
        fs::path n = p.lexically_normal();
        return n == n.root_path();
    }

    // Read an environment variable without the MSVC C4996 deprecation that
    // std::getenv trips under /W4 (mirrors AppData.cpp's _dupenv_s usage).
    std::string EnvVar(const char* name)
    {
        char*  value = nullptr;
        size_t len   = 0;
        std::string out;
        if (_dupenv_s(&value, &len, name) == 0 && value)
            out = value;
        free(value);
        return out;
    }
}

std::vector<DirEntry> ListDirectory(const std::string& dir,
                                    const std::vector<std::string>& fileExts,
                                    bool dirsOnly)
{
    std::vector<DirEntry> dirs, files;

    std::error_code ec;
    fs::path base(dir);
    fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    if (!ec)
    {
        for (fs::directory_iterator end; it != end; it.increment(ec))
        {
            if (ec) break;
            std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;   // skip dotfiles

            std::error_code ec2;
            bool isDir = it->is_directory(ec2);
            if (ec2) continue;

            if (isDir) { dirs.push_back({ name, true }); continue; }
            if (dirsOnly) continue;

            if (!fileExts.empty())
            {
                std::string ext = ToLower(it->path().extension().string());
                if (std::find(fileExts.begin(), fileExts.end(), ext) == fileExts.end())
                    continue;
            }
            files.push_back({ name, false });
        }
    }

    std::sort(dirs.begin(),  dirs.end(),  NameLess);
    std::sort(files.begin(), files.end(), NameLess);

    std::vector<DirEntry> out;
    if (!IsRootPath(base)) out.push_back({ "..", true });
    out.insert(out.end(), dirs.begin(),  dirs.end());
    out.insert(out.end(), files.begin(), files.end());
    return out;
}

std::string ParentDirectory(const std::string& dir)
{
    fs::path p = fs::path(dir).lexically_normal();
    if (IsRootPath(p)) return p.string();
    fs::path parent = p.parent_path();
    return parent.empty() ? p.string() : parent.string();
}

std::string JoinPath(const std::string& dir, const std::string& name)
{
    return (fs::path(dir) / name).string();
}

bool IsDirectory(const std::string& path)
{
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec) && !ec;
}

std::string DefaultBrowseDir(const std::string& currentDocPath)
{
    std::error_code ec;

    if (!currentDocPath.empty())
    {
        fs::path parent = fs::path(currentDocPath).parent_path();
        if (!parent.empty() && fs::is_directory(parent, ec))
            return parent.string();
    }

    std::string home = EnvVar("USERPROFILE");
    if (!home.empty())
    {
        fs::path docs = fs::path(home) / "Documents";
        if (fs::is_directory(docs, ec)) return docs.string();
        if (fs::is_directory(fs::path(home), ec)) return home;
    }

    std::string cwd = fs::current_path(ec).string();
    return ec ? std::string() : cwd;
}

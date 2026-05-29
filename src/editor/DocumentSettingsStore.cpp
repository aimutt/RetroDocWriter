#include "DocumentSettingsStore.h"
#include "platform/AppData.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    constexpr const char* kSourcePathKey      = "source_path";
    constexpr const char* kFirstMissingAtKey  = "first_missing_at";

    // 64-bit string hash → 16-char hex filename stem. Collisions are
    // theoretically possible but practically harmless because the entry
    // file carries its own authoritative `source_path` — a collision just
    // means the rare other document overwrites this entry the next time
    // it's opened. (Real-world collision rate at this hash width is
    // effectively zero for plausible document counts.)
    std::string HashToHex(const std::string& s)
    {
        const size_t h = std::hash<std::string>{}(s);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%016zx", h);
        return buf;
    }

    std::string EntryFilePath(const std::string& canonicalPath)
    {
        std::string dir = DocumentStoreDir();
        if (dir.empty()) return {};
        return dir + HashToHex(canonicalPath) + ".ini";
    }

    // True if `path` resolves to an existing file. Uses GetFileAttributesA
    // (cheap, sync) and treats directories as "not the file we want".
    bool FileExists(const std::string& path)
    {
        if (path.empty()) return false;
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return false;
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    // Returns the parent directory portion of `path` (everything before the
    // last separator). When `path` has no separator, returns empty.
    std::string ParentDir(const std::string& path)
    {
        size_t slash = path.find_last_of("\\/");
        if (slash == std::string::npos) return {};
        return path.substr(0, slash);
    }

    bool DirExists(const std::string& path)
    {
        if (path.empty()) return false;
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return false;
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
}

std::string DocumentSettingsStore::CanonicalizePath(const std::string& userPath)
{
    if (userPath.empty()) return {};
    char buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameA(userPath.c_str(),
                               static_cast<DWORD>(sizeof(buf)), buf, nullptr);
    std::string canon;
    if (n == 0 || n >= sizeof(buf))
        canon = userPath;          // fall back to the user-supplied path
    else
        canon.assign(buf, n);
    // Case-insensitive FS on Windows: normalize so `Foo.rtf` and `foo.rtf`
    // are one entry, not two.
    std::transform(canon.begin(), canon.end(), canon.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return canon;
}

bool DocumentSettingsStore::ReadFor(const std::string& sourcePath, FileSettings& out)
{
    std::string canon = CanonicalizePath(sourcePath);
    if (canon.empty()) return false;
    std::string entry = EntryFilePath(canon);
    if (entry.empty()) return false;

    if (!out.Load(entry)) return false;

    // `source_path` / `first_missing_at` ride along in the returned
    // FileSettings but ApplyFileSettings only looks at the keys it knows
    // about, so the bookkeeping keys are silently ignored downstream.
    return true;
}

bool DocumentSettingsStore::WriteFor(const std::string& sourcePath, const FileSettings& in)
{
    std::string canon = CanonicalizePath(sourcePath);
    if (canon.empty()) return false;
    std::string entry = EntryFilePath(canon);
    if (entry.empty()) return false;

    FileSettings out = in;
    out.SetString(kSourcePathKey, canon);
    out.SetString(kFirstMissingAtKey, "0");
    return out.Save(entry);
}

void DocumentSettingsStore::CollectGarbage(int graceDays)
{
    std::string dir = DocumentStoreDir();
    if (dir.empty()) return;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) return;

    const std::time_t nowSec = std::time(nullptr);
    const std::time_t graceSec =
        static_cast<std::time_t>(graceDays) * 24 * 60 * 60;

    for (const auto& dent : it)
    {
        if (!dent.is_regular_file(ec)) continue;
        const std::string entryPath = dent.path().string();
        if (entryPath.size() < 4
            || entryPath.compare(entryPath.size() - 4, 4, ".ini") != 0)
            continue;

        FileSettings s;
        if (!s.Load(entryPath))
            continue;

        std::string src = s.GetString(kSourcePathKey, "");
        if (src.empty())
        {
            // Corrupt / pre-bookkeeping entry — purge.
            std::remove(entryPath.c_str());
            continue;
        }

        // Safety: if we can't even see the parent directory (network share
        // offline, drive ejected), don't penalize the entry — try again on
        // the next launch.
        std::string parent = ParentDir(src);
        if (!parent.empty() && !DirExists(parent))
            continue;

        const bool exists = FileExists(src);
        std::time_t firstMissing = 0;
        try { firstMissing = static_cast<std::time_t>(std::stoll(
                  s.GetString(kFirstMissingAtKey, "0"))); }
        catch (...) { firstMissing = 0; }

        if (exists)
        {
            // File came back (or never left). Clear the missing timestamp
            // if it had been set so the grace window resets cleanly.
            if (firstMissing != 0)
            {
                s.SetString(kFirstMissingAtKey, "0");
                s.Save(entryPath);
            }
            continue;
        }

        // File is missing — either start the grace window or, if it's
        // expired, delete the entry.
        if (firstMissing == 0)
        {
            s.SetString(kFirstMissingAtKey, std::to_string(nowSec));
            s.Save(entryPath);
        }
        else if (nowSec - firstMissing >= graceSec)
        {
            std::remove(entryPath.c_str());
        }
    }
}

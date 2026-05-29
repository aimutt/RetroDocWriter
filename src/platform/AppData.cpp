#include "AppData.h"
#include <cstdlib>
#include <direct.h>

namespace
{
    // Reads an environment variable into a std::string. Empty when the
    // variable is unset or `_dupenv_s` fails.
    std::string GetEnvVar(const char* name)
    {
        char* raw = nullptr;
        size_t len = 0;
        if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr)
            return {};
        std::string out(raw);
        free(raw);
        return out;
    }
}

std::string UserConfigDir()
{
    std::string dir = GetEnvVar("APPDATA");
    if (dir.empty()) return {};

    if (dir.back() != '\\' && dir.back() != '/')
        dir.push_back('\\');
    dir += "RetroDocWriter\\";

    // Best-effort mkdir; existing dir returns -1 with errno=EEXIST which is fine.
    _mkdir(dir.c_str());
    return dir;
}

std::string DocumentStoreDir()
{
    std::string base = UserConfigDir();
    if (base.empty()) return {};
    std::string dir = base + "documents\\";
    _mkdir(dir.c_str());
    return dir;
}

std::string LegacyUserConfigDir()
{
    std::string dir = GetEnvVar("LOCALAPPDATA");
    if (dir.empty()) return {};
    if (dir.back() != '\\' && dir.back() != '/')
        dir.push_back('\\');
    dir += "RetroEdit\\";
    return dir;   // do not _mkdir — only used to read legacy files if they exist
}

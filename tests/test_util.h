#pragma once
// Small filesystem helpers for tests that exercise on-disk round-trips. Each
// TempDir creates a unique directory under the OS temp location and removes it
// (recursively) on scope exit, so tests never touch the user's real data.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

struct TempDir
{
    std::filesystem::path path;

    TempDir()
    {
        static std::atomic<unsigned long long> counter{ 0 };
        std::error_code ec;
        std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        unsigned long long n = counter.fetch_add(1);
        do {
            path = base / ("rdwtest_"
                + std::to_string(reinterpret_cast<std::uintptr_t>(this))
                + "_" + std::to_string(n++));
        } while (std::filesystem::exists(path, ec));
        std::filesystem::create_directories(path, ec);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string str() const { return path.string(); }
    std::string file(const std::string& name) const
    {
        return (path / name).string();
    }
    void makeSubdir(const std::string& name) const
    {
        std::error_code ec;
        std::filesystem::create_directories(path / name, ec);
    }
};

inline void WriteTextFile(const std::string& p, const std::string& content)
{
    std::ofstream f(p, std::ios::binary);
    f << content;
}

inline std::string ReadTextFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

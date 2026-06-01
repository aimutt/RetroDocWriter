#pragma once
#include <string>
#include <vector>

// Filesystem helpers backing the in-app file/folder browser (Open / Save As
// "Browse..."). SDL-free — pure std::filesystem + Win32 env, so it carries no
// rendering or SDL dependency and can be unit-tested in isolation.

struct DirEntry
{
    std::string name;          // bare name (no path); ".." for the parent link
    bool        isDir = false;
};

// List `dir`'s contents for the browser. Directories sort first, then files,
// each case-insensitively; a ".." entry is prepended unless `dir` is a
// filesystem root. Dotfiles are skipped. When `dirsOnly` is true only
// directories are returned (folder-pick / Save mode). Otherwise files are
// included, filtered to `fileExts` (lowercase, e.g. {".rtf",".txt"}); an empty
// `fileExts` keeps every file. Returns {} (plus ".." when not root) on an
// unreadable/missing directory rather than throwing.
std::vector<DirEntry> ListDirectory(const std::string& dir,
                                    const std::vector<std::string>& fileExts,
                                    bool dirsOnly);

// Parent of `dir`; a root path returns itself (so "go up" at a drive root is a
// no-op rather than an error).
std::string ParentDirectory(const std::string& dir);

// `dir` + path separator + `name`.
std::string JoinPath(const std::string& dir, const std::string& name);

// True if `path` exists and is a directory.
bool IsDirectory(const std::string& path);

// A sensible starting directory for the browser: the directory of
// `currentDocPath` if it names a real file location, else the user's Documents
// folder, else their home, else the process current directory.
std::string DefaultBrowseDir(const std::string& currentDocPath);

#pragma once
#include "FileSettings.h"
#include <string>

// Central per-document settings store. Replaces the legacy
// `<documentPath>.retroedit` sidecar with one entry file per known document
// kept under %APPDATA%\RetroDocWriter\documents\. Each entry file uses the
// same key=value format as FileSettings (same parser, same value codec) plus
// two reserved bookkeeping keys at the top:
//   source_path = <absolute canonical path of the document>
//   first_missing_at = 0 (or a Unix timestamp set by GC)
// The store is keyed by a hash of the canonical (lowercased, absolute) path,
// so a `<hash>.ini` filename is deterministic per document — but the
// authoritative source-of-truth for "which document does this entry belong
// to" is `source_path` inside the file.
class DocumentSettingsStore
{
public:
    // Look up the entry for `sourcePath`. Populates `out` with the document's
    // user-facing settings (the two bookkeeping keys are stripped). Returns
    // false when no entry exists for that path.
    static bool ReadFor(const std::string& sourcePath, FileSettings& out);

    // Write (or overwrite) the entry for `sourcePath`. `in` carries the
    // user-facing settings; this helper adds the bookkeeping keys and resets
    // `first_missing_at` to 0 (any open file proves the source still exists).
    static bool WriteFor(const std::string& sourcePath, const FileSettings& in);

    // One garbage-collection pass over every entry in the documents directory.
    // For each entry whose source file has gone missing, record the first
    // time we noticed (`first_missing_at`); if that timestamp is older than
    // `graceDays`, delete the entry. Sources whose parent directory cannot
    // be stat'd (e.g., a disconnected network drive) are skipped — never
    // GC'd — so an unreachable share doesn't lose state.
    static void CollectGarbage(int graceDays);

    // Returns the canonical (absolute, lowercased on Windows) form of
    // `userPath`. Used as the keying input for the hash. Exposed mostly for
    // tests and diagnostics.
    static std::string CanonicalizePath(const std::string& userPath);
};

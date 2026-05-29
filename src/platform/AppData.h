#pragma once
#include <string>

// Returns the user-config directory for RetroDocWriter, creating it if missing.
// On Windows: "%APPDATA%\RetroDocWriter\" (the Roaming root, with trailing
// separator). Returns an empty string if the directory cannot be located or
// created; callers must treat that as "persistence disabled" and no-op
// gracefully.
std::string UserConfigDir();

// Returns the per-document settings subdirectory (UserConfigDir + "documents\"),
// creating it if missing. Used by DocumentSettingsStore for the central
// replacement of the legacy `.retroedit` sidecar files. Empty on failure.
std::string DocumentStoreDir();

// Returns the legacy %LOCALAPPDATA%\RetroEdit\ path (without creating it).
// Used once on startup to migrate config.ini / user_dictionary.txt forward
// to the new UserConfigDir(). Empty when the legacy directory is unavailable.
std::string LegacyUserConfigDir();

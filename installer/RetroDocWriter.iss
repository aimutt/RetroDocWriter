; Inno Setup script for RetroDocWriter.
; Build with:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\RetroDocWriter.iss
; Produces:    dist\RetroDocWriter-Setup-<AppVersion>.exe  (e.g. ...-0.2.0.exe)
;
; The output filename carries the version, so building a new release writes a
; NEW file beside the older installers in dist\ — it never overwrites them.
; Keeping every version's installer lets a user uninstall a build they dislike
; and reinstall an older version from its own setup .exe.
;
; Prerequisite: a Release build must exist (cmake --build build --config Release),
; which leaves RetroDocWriter.exe and the three SDL DLLs in build\Release.
; Assets are bundled from the clean repo source tree (assets\), NOT from
; build\Release\assets, to avoid shipping stray test files left in the build dir.

#define AppName "RetroDocWriter"
#define AppVersion "0.2.0"
#define AppPublisher "aimutt.com"
#define AppURL "https://aimutt.com/projects/retrodocwriter.html"
#define AppExe "RetroDocWriter.exe"
; AppId GUID — kept identical across releases so a new version overwrites the
; old one in place (no side-by-side installs) and the [Code] section can find
; the existing install's uninstall registry key. Reused below as {#AppId}_is1.
#define AppId "{B8F4B2A1-7C3E-4D9A-9E21-1A2B3C4D5E6F}"

[Setup]
AppId={{#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\assets\screenshots\aimutt-logo.ico
LicenseFile=..\LICENSE.txt
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
; Version-stamped so each release is a distinct file (e.g. RetroDocWriter-Setup-0.2.0.exe).
OutputBaseFilename=RetroDocWriter-Setup-{#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\build\Release\{#AppExe}";      DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\SDL3.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\SDL3_ttf.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\SDL3_image.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE.txt";             DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\RELEASE-NOTES.txt";       DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";       Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Optional post-install checkbox to read what changed since the last version.
; 'unchecked' = off by default; shellexec opens it in the default .txt handler.
Filename: "{app}\RELEASE-NOTES.txt"; Description: "View release notes (what's new in {#AppVersion})"; Flags: postinstall shellexec skipifsilent unchecked nowait
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
{ Detect a previous RetroDocWriter install (same AppId) and ask whether to
  update to this version. Choosing No aborts setup; the user's installed build
  is left untouched. A fresh machine (no prior install) skips the prompt. }
function InitializeSetup(): Boolean;
var
  UninstKey: String;
  OldVersion: String;
begin
  Result := True;
  UninstKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1';
  if RegQueryStringValue(HKLM, UninstKey, 'DisplayVersion', OldVersion) or
     RegQueryStringValue(HKCU, UninstKey, 'DisplayVersion', OldVersion) then
  begin
    if MsgBox('RetroDocWriter ' + OldVersion + ' is already installed.' + #13#10 + #13#10 +
              'Update to version {#AppVersion}? The existing version will be replaced.',
              mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

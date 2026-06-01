; Inno Setup script for RetroDocWriter.
; Build with:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\RetroDocWriter.iss
; Produces:    dist\RetroDocWriter-Setup.exe
;
; Prerequisite: a Release build must exist (cmake --build build --config Release),
; which leaves RetroDocWriter.exe and the three SDL DLLs in build\Release.
; Assets are bundled from the clean repo source tree (assets\), NOT from
; build\Release\assets, to avoid shipping stray test files left in the build dir.

#define AppName "RetroDocWriter"
#define AppVersion "0.1.0"
#define AppPublisher "aimutt.com"
#define AppURL "https://aimutt.com/projects/retrodocwriter.html"
#define AppExe "RetroDocWriter.exe"

[Setup]
AppId={{B8F4B2A1-7C3E-4D9A-9E21-1A2B3C4D5E6F}
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
OutputBaseFilename=RetroDocWriter-Setup

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

[Icons]
Name: "{group}\{#AppName}";       Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

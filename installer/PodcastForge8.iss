; Podcast Forge 8 installer (Inno Setup 6). Built by scripts\package.ps1, which passes /DAppVersion.
;
; Per-user by default (no administrator rights needed); "all users" is offered when elevated.
; The uninstaller removes only the files this installer placed. It never touches:
;   Documents\PodcastForge8\           projects and recordings
;   %LOCALAPPDATA%\PodcastForge8\      settings (Settings.db), logs, diagnostics reports
; so reinstalling or upgrading keeps everything.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "Podcast Forge 8"
#define AppExe "PodcastForge8.exe"
#define BuildDir "..\build\release\bin"

[Setup]
AppId={{5E7C3B1A-8F2D-4C6E-9A41-0F8C0D5E7A18}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=unupunct
AppPublisherURL=https://github.com/unupunct/Podcast-Forge-8
AppSupportURL=https://github.com/unupunct/Podcast-Forge-8/issues
AppUpdatesURL=https://github.com/unupunct/Podcast-Forge-8/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\out
OutputBaseFilename=PodcastForge8-{#AppVersion}-Setup-x64
SetupIconFile=..\resources\PodcastForge8.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\LIMITATIONS.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\OBS.md"; DestDir: "{app}\docs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\docs\PROJECTS.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\CHANGELOG.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; No [UninstallDelete] and no [Registry] entries: user data is never removed (see the header).

[Messages]
FinishedLabel=Setup has finished installing [name] on your computer.%n%nYour projects are saved in Documents\PodcastForge8. Uninstalling never deletes projects, recordings or settings.

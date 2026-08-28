; Inno Setup script for hyperwave -- the direct-download Windows installer.
;
; Expects a staging directory that windeployqt has already populated, with
; WinSparkle.dll beside the executable. CI passes both in:
;
;   iscc /DAppVersion=0.1.0 /DStagingDir=dist\windows ^
;        /Obuild packaging\windows\hyperwave.iss
;
; This is also what WinSparkle downloads and runs to apply an update, which is
; why the appcast's enclosure is this file rather than the MSIX: an MSIX is
; installed by the Store machinery, not by a downloaded updater.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StagingDir
  #define StagingDir "dist\windows"
#endif

[Setup]
AppName=hyperwave
AppVersion={#AppVersion}
AppVerName=hyperwave {#AppVersion}
AppPublisher=Hypernuclear LLC
AppPublisherURL=https://hypernuclear.com
DefaultDirName={autopf}\hyperwave
DefaultGroupName=hyperwave
OutputBaseFilename=hyperwave-setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
SetupIconFile=..\..\resources\hyperwave.ico
UninstallDisplayIcon={app}\hyperwave.exe
ArchitecturesAllowed=x64compatible arm64
ArchitecturesInstallIn64BitMode=x64compatible arm64
WizardStyle=modern
; No admin prompt. This installs per-user into the profile, which is all a
; notification-area app needs, and an elevation prompt is a reason to close the
; installer. It also matters for the appbar: registering one is a per-user,
; per-session thing, so nothing here wants elevation.
PrivilegesRequired=lowest
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Ticked by default, unlike the desktop icon: hyperwave has no window, so a
; copy that is not running is one the user will assume is broken.
Name: "startup"; Description: "Start hyperwave when I sign in"; \
  GroupDescription: "Startup"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything windeployqt staged. The MSIX-only pieces are excluded so a user
; installing the EXE does not get a manifest and a set of Store tiles dropped
; into their Program Files.
Source: "{#StagingDir}\*"; DestDir: "{app}"; \
  Flags: ignoreversion recursesubdirs createallsubdirs; \
  Excludes: "AppxManifest.xml,Assets"

[Icons]
Name: "{group}\hyperwave"; Filename: "{app}\hyperwave.exe"
Name: "{autodesktop}\hyperwave"; Filename: "{app}\hyperwave.exe"; Tasks: desktopicon
Name: "{userstartup}\hyperwave"; Filename: "{app}\hyperwave.exe"; Tasks: startup

[Run]
Filename: "{app}\hyperwave.exe"; Description: "{cm:LaunchProgram,hyperwave}"; \
  Flags: nowait postinstall skipifsilent

[UninstallRun]
; An appbar that is still registered when its process dies leaves its reserved
; strip behind, and the space does not come back without a reboot. hyperwave
; sends ABM_REMOVE on its own teardown, so the uninstaller's job is only to make
; sure teardown actually happens rather than the process being killed under it.
Filename: "taskkill.exe"; Parameters: "/IM hyperwave.exe"; \
  Flags: runhidden skipifdoesntexist; RunOnceId: "StopHyperwave"

; Stemmerizer · Inno Setup script
; ----------------------------------
; Builds a single .exe installer that lays down:
;   - Stemmerizer.vst3      → %CommonProgramFiles%\VST3\
;   - Stemmerizer.exe       → install dir (standalone)
;   - weights\*.gguf        → install dir\weights\
;   - legal\*.md            → install dir\legal\
;   - Microsoft VC++ Redist → only if missing
;
; Build: ISCC.exe Stemmerizer.iss
; Sign:  pass /SIGNTOOL=signtool to bake the cert in. (See README-BUILD.md)

#define AppName       "Stemmerizer"
#define AppVersion    "1.0.0"
#define AppPublisher  "NVA Audio"
#define AppURL        "https://nvaaudio.com/stemmerizer"
#define AppExeName    "Stemmerizer.exe"
#define BuildDir      "..\build\Stemmerizer_artefacts\Release"

[Setup]
AppId={{B6F1A9D2-3C4F-4E1E-9D11-7F43A9E2C810}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/support
AppUpdatesURL={#AppURL}/downloads
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
DisableReadyPage=no
LicenseFile=..\legal\EULA.md
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
OutputDir=output
OutputBaseFilename=Stemmerizer-{#AppVersion}-Windows-x64-Setup
; SetupIconFile=..\resources\images\stemmerizer.ico    ; uncomment once the .ico exists
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany={#AppPublisher}
VersionInfoProductName={#AppName}
SignTool={#emit GetEnv('SIGNTOOL')}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "core";       Description: "Plugin core (required)";  Types: full custom; Flags: fixed
Name: "vst3";       Description: "VST3 plug-in";            Types: full
Name: "standalone"; Description: "Standalone application";  Types: full
Name: "models";     Description: "AI models (~270 MB)";     Types: full custom; Flags: fixed
Name: "models\fast";  Description: "4-stem fast (htdemucs)";       Types: full custom
Name: "models\hq";    Description: "4-stem high quality (htdemucs_ft)"; Types: full
Name: "models\6stem"; Description: "6-stem (htdemucs_6s)";  Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone"; GroupDescription: "Additional shortcuts:"; Components: standalone

[Files]
; Plugin VST3 — JUCE produces a directory bundle on Windows. Inno copies it recursively.
Source: "{#BuildDir}\VST3\Stemmerizer.vst3\*"; DestDir: "{commoncf}\VST3\Stemmerizer.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3

; Standalone exe
Source: "{#BuildDir}\Standalone\Stemmerizer.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

; Weights
Source: "..\resources\weights\htdemucs.gguf";    DestDir: "{app}\weights"; Flags: ignoreversion; Components: models\fast
Source: "..\resources\weights\htdemucs_ft.gguf"; DestDir: "{app}\weights"; Flags: ignoreversion skipifsourcedoesntexist; Components: models\hq
Source: "..\resources\weights\htdemucs_6s.gguf"; DestDir: "{app}\weights"; Flags: ignoreversion skipifsourcedoesntexist; Components: models\6stem

; Legal
Source: "..\legal\EULA.md";          DestDir: "{app}\legal"; Flags: ignoreversion; Components: core
Source: "..\legal\PRIVACY.md";       DestDir: "{app}\legal"; Flags: ignoreversion; Components: core
Source: "..\legal\NOTICE.md";        DestDir: "{app}\legal"; Flags: ignoreversion; Components: core
Source: "..\legal\LICENSE-AUDIT.md"; DestDir: "{app}\legal"; Flags: ignoreversion; Components: core
Source: "..\LICENSE";                DestDir: "{app}";       Flags: ignoreversion; Components: core

; VC++ runtime (only run if not already installed; logic below)
Source: "redist\VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist; Components: core

[Icons]
Name: "{group}\{#AppName}";        Filename: "{app}\{#AppExeName}"; Components: standalone
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    Check: VCRedistMissing; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Components: core
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent; Components: standalone

[Code]
function VCRedistMissing(): Boolean;
var
  Installed: Cardinal;
begin
  // Microsoft x64 redistributable registry probe.
  Result := True;
  if RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
                        'Installed', Installed) then
  begin
    if Installed = 1 then Result := False;
  end;
end;

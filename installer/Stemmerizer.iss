; Stemmerizer · Inno Setup script
; ----------------------------------
; Builds a single .exe installer that lays down:
;   - Stemmerizer.vst3      → %CommonProgramFiles%\VST3\
;   - Stemmerizer.exe       → install dir (standalone)
;   - weights\*.onnx + *.onnx.data → %APPDATA%\Stemmerizer\weights\
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
; Plugin VST3 — JUCE produces a directory bundle on Windows; Inno copies
; it recursively. This brings the .vst3 module AND stemonnx.dll (our
; renamed ONNX Runtime, copied beside the VST3 by the CMake PRE_LINK
; step) into Program Files in one shot.
Source: "{#BuildDir}\VST3\Stemmerizer.vst3\*"; DestDir: "{commoncf}\VST3\Stemmerizer.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3

; Standalone exe + its own copy of stemonnx.dll (same reasoning: the
; CMake post-build puts it beside Stemmerizer.exe).
Source: "{#BuildDir}\Standalone\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs; Components: standalone

; AI model weights. Demucs v4 ships as a tiny graph file (.onnx) plus
; an external data blob (.onnx.data) holding ~168 MB of weights — BOTH
; must land in the same directory or ORT can't resolve the external data.
;
; Destination: %APPDATA%\Stemmerizer\weights\ — this is the per-user
; data folder our PluginProcessor::resolveWeightsDir() looks at by
; default. It also survives uninstall/reinstall cycles, so users don't
; re-download 270 MB every time they update the plugin.
Source: "..\resources\weights\htdemucs.onnx";        DestDir: "{userappdata}\Stemmerizer\weights"; Flags: ignoreversion; Components: models\fast
Source: "..\resources\weights\htdemucs.onnx.data";   DestDir: "{userappdata}\Stemmerizer\weights"; Flags: ignoreversion; Components: models\fast
Source: "..\resources\weights\htdemucs_ft.onnx";     DestDir: "{userappdata}\Stemmerizer\weights"; Flags: ignoreversion skipifsourcedoesntexist; Components: models\hq
Source: "..\resources\weights\htdemucs_ft.onnx.data";DestDir: "{userappdata}\Stemmerizer\weights"; Flags: ignoreversion skipifsourcedoesntexist; Components: models\hq
Source: "..\resources\weights\htdemucs_6s.onnx";     DestDir: "{userappdata}\Stemmerizer\weights"; Flags: ignoreversion skipifsourcedoesntexist; Components: models\6stem
Source: "..\resources\weights\htdemucs_6s.onnx.data";DestDir: "{userappdata}\Stemmerizer\weights"; Flags: ignoreversion skipifsourcedoesntexist; Components: models\6stem

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

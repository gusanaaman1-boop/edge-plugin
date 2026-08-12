; EDGE — Windows installer, for Inno Setup 6.
;
;   iscc packaging\EDGE.iss
;
; Run build-windows.bat FIRST. This script installs what that produced; it does
; not build anything itself, so an installer can never ship a binary nobody
; tested.
;
; NOT verified: there is no Windows machine here. Everything below is written
; to be checked on the first Windows run, and the script fails loudly rather
; than installing half of itself.

#define AppName    "EDGE"
#define AppPublisher "Naaman"
#define AppVersion "0.14.0"
#define SrcRoot    "..\build\Edge_artefacts\Release"

[Setup]
AppId={{8F2C4A61-3D7E-4B92-9C15-EDGE0000A001}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=EDGE-{#AppVersion}-windows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Writing to Common Files needs elevation. Asking for it up front is honest;
; discovering it half way through a copy is not.
PrivilegesRequired=admin

; Refuse rather than corrupt: a VST3 folder that is half replaced because the
; DAW had it open is a plug-in that crashes on next scan.
CloseApplications=yes
RestartApplications=no

[Types]
Name: "full";   Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Cubase, Live, Reaper, Bitwig)"; Types: full custom; Flags: checkablealone
Name: "app";  Description: "Standalone application";                      Types: full custom

[Files]
; The VST3 is a BUNDLE - a folder, not a file. recursesubdirs is what makes it
; arrive intact; copying only the .vst3 file inside it produces a plug-in that
; scans and then fails to load.
Source: "{#SrcRoot}\VST3\EDGE.vst3\*"; \
    DestDir: "{commoncf64}\VST3\EDGE.vst3"; \
    Components: vst3; \
    Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#SrcRoot}\Standalone\EDGE.exe"; \
    DestDir: "{app}"; \
    Components: app; \
    Flags: ignoreversion

Source: "..\docs\MANUAL.md";          DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\PARAMETER-TABLE.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\EDGE.exe"; Components: app

[UninstallDelete]
; The bundle folder itself, which Inno leaves behind once its files are gone.
Type: dirifempty; Name: "{commoncf64}\VST3\EDGE.vst3"

[Code]
// Fail before copying anything, not after. build-windows.bat produces these;
// if they are missing the user ran the wrong thing, and saying so is far more
// use than an installer that succeeds and installs nothing.
function InitializeSetup(): Boolean;
var
  Missing: String;
begin
  Missing := '';

  if not DirExists(ExpandConstant('{src}\..\build\Edge_artefacts\Release\VST3\EDGE.vst3')) then
    Missing := Missing + '  build\Edge_artefacts\Release\VST3\EDGE.vst3' + #13#10;

  Result := (Missing = '');

  if not Result then
    MsgBox('EDGE has not been built yet.' + #13#10#13#10 +
           'Missing:' + #13#10 + Missing + #13#10 +
           'Run build-windows.bat first — it builds EDGE and runs its test ' +
           'suites before anything is installed.',
           mbCriticalError, MB_OK);
end;

// Verify, do not assume. An installer that reports success while leaving the
// plug-in folder empty is the single most expensive kind of silent failure.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Target: String;
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsComponentSelected('vst3') then
    begin
      Target := ExpandConstant('{commoncf64}\VST3\EDGE.vst3');
      if not DirExists(Target) then
        MsgBox('INSTALL FAILED: ' + Target + ' does not exist afterwards.' + #13#10 +
               'Nothing was installed for Cubase. Try running this installer ' +
               'as administrator.', mbCriticalError, MB_OK);
    end;
  end;
end;

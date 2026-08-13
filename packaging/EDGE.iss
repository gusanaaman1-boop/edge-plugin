; EDGE - Windows installer, for Inno Setup 6.
;
; Do not run this by hand. MAKE-INSTALLER.bat builds EDGE and then calls this
; with the version, the artefact folder and the icon already worked out:
;
;     MAKE-INSTALLER.bat   ->   dist\EDGE-0.18.0-windows.exe
;
; It packs what the build produced; it never builds anything itself, so an
; installer can never ship a binary nobody compiled.
;
; NOT verified: there is no Windows machine here. Everything below is written to
; be checked on the first Windows run, and to fail loudly - at COMPILE time
; where possible - rather than install half of itself.

#ifndef AppVersion
  #define AppVersion "0.18.0"
#endif

; Where the built artefacts live. MAKE-INSTALLER.bat passes an absolute path;
; the default is the layout the Visual Studio generator produces.
#ifndef SrcRoot
  #define SrcRoot "..\build-win\Edge_artefacts\Release"
#endif

#define AppName      "EDGE"
#define AppPublisher "Naaman"
#define AppAuthor    "Gussa Naaman"

; Say which tool this needs, in words, before Inno 5 chokes on a directive it
; has never heard of and reports a line number instead of a cause.
#if VER < EncodeVer(6,0,0)
  #error EDGE's installer needs Inno Setup 6. Get it from https://jrsoftware.org/isdl.php
#endif

; --- compile-time proof that there is something to pack ----------------------
; The old version of this file checked for the build tree in InitializeSetup -
; that is, on the END USER'S machine, at install time, where the build tree of
; course does not exist. Every recipient would have been told "EDGE has not been
; built yet" and the installer would have refused itself. The check belongs
; here, when the package is made, and it is a hard error.
#if !DirExists(SrcRoot + "\VST3\EDGE.vst3")
  #error The VST3 bundle is missing. Run MAKE-INSTALLER.bat, which builds first.
#endif

; The bundle FOLDER existing proves nothing - a build that died half way leaves
; an empty one, and it installs perfectly and then fails to load.
#if !FileExists(SrcRoot + "\VST3\EDGE.vst3\Contents\x86_64-win\EDGE.vst3")
  #error The VST3 bundle is empty - the build did not finish. See EDGE-installer-log.txt.
#endif

#if !FileExists(SrcRoot + "\Standalone\EDGE.exe")
  #error The standalone was not built. Run MAKE-INSTALLER.bat.
#endif

[Setup]
AppId={{8F2C4A61-3D7E-4B92-9C15-EDGE0000A001}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright={#AppAuthor}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=EDGE - a two-sided musical filter
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=EDGE-{#AppVersion}-windows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; "x64" and not "x64compatible": the newer spelling is only understood by Inno
; Setup 6.3 and later, and refusing to compile on 6.0 is a worse trade than the
; deprecation warning the newer compilers print for this one. Both mean the same
; thing here - EDGE is 64-bit only.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

; What the wizard says before anything is chosen. A plug-in installer that opens
; with "Welcome to the EDGE Setup Wizard" and nothing else leaves the one
; question a musician actually has - where does this end up - unanswered.
InfoBeforeFile=INFO-BEFORE.txt

; The mark, if the build produced one. JUCE generates icon.ico from ICON_BIG,
; and MAKE-INSTALLER.bat only passes it once it exists: Inno refuses to compile
; against a SetupIconFile that is not there.
#ifdef EdgeIcon
SetupIconFile={#EdgeIcon}
#endif
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\EDGE.exe

; Writing to Common Files needs elevation. Asking for it up front is honest;
; discovering it half way through a copy is not.
PrivilegesRequired=admin

; Refuse rather than corrupt: a VST3 folder that is half replaced because the
; DAW had it open is a plug-in that crashes on the next scan.
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

; The manual goes in whatever was installed, so it is never orphaned.
Source: "..\docs\MANUAL.md";          DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\PARAMETER-TABLE.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\EDGE.exe"; Components: app

[UninstallDelete]
; The bundle folder itself, which Inno leaves behind once its files are gone.
Type: dirifempty; Name: "{commoncf64}\VST3\EDGE.vst3"

[Code]
// Verify, do not assume. An installer that reports success while leaving the
// plug-in folder empty is the single most expensive kind of silent failure -
// the user rescans, finds nothing, and has no idea which half went wrong.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Target: String;
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsComponentSelected('vst3') then
    begin
      Target := ExpandConstant('{commoncf64}\VST3\EDGE.vst3\Contents\x86_64-win\EDGE.vst3');
      if not FileExists(Target) then
        MsgBox('INSTALL FAILED: the plug-in is not at' + #13#10 + Target + #13#10#13#10 +
               'Nothing usable was installed for Cubase. Try running this ' +
               'installer again as administrator, with your DAW closed.',
               mbCriticalError, MB_OK);
    end;
  end;
end;

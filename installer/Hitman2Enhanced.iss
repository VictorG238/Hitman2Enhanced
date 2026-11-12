; Hitman 2 Enhanced - Windows installer (Inno Setup 6). Saved as UTF-8 with BOM (Spanish texts).
;
; Build (after `python -I tools/package_release.py`, which fills dist\payload, and the PyInstaller step of
; .github/workflows/build.yml, which adds dist\payload\hdfonts_build.exe):
;   iscc /DAppVersion=0.8.0 installer\Hitman2Enhanced.iss
; Output: dist\Hitman2Enhanced-Setup-<version>.exe
;
; What it does: finds the Steam install of Hitman 2: Silent Assassin (or lets the player choose the folder that
; contains hitman2.exe), copies d3d8.dll + h2e.ini next to hitman2.exe and registers an uninstaller. It never touches
; the game's own files. It refuses to overwrite a d3d8.dll that belongs to another mod, and keeps an existing h2e.ini
; (the player's configuration) on update.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef PayloadDir
  #define PayloadDir "..\dist\payload"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
; Subfolder of the game folder for the uninstaller, docs and the font builder. Must not be "Hitman2Enhanced": that is
; the usual name of a development checkout inside the game folder.
#define SubDir "Hitman 2 Enhanced"
#define Author "Aquiles / Victor Baeza"
#define AuthorUrl "https://github.com/VictorG238"
#define AuthorUrlShort "github.com/VictorG238"
#define RepoUrl "https://github.com/VictorG238/Hitman2Enhanced"
#define RepoUrlShort "github.com/VictorG238/Hitman2Enhanced"

[Setup]
AppId={{6B0E2D5C-8F3A-4C57-9E2B-1A2E3D4C5B6F}
AppName=Hitman 2 Enhanced
AppVersion={#AppVersion}
AppVerName=Hitman 2 Enhanced {#AppVersion}
AppPublisher={#Author}
AppPublisherURL={#AuthorUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
AppCopyright=Copyright (c) 2026 {#Author} - MIT License
AppComments=Unofficial fixes for Hitman 2: Silent Assassin (Steam)
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#Author}
VersionInfoDescription=Hitman 2 Enhanced installer
DefaultDirName={code:DefaultGameDir}
AppendDefaultDirName=no
DirExistsWarning=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
; Uninstaller and docs go into a subfolder of the game folder, so only d3d8.dll and h2e.ini sit next to hitman2.exe.
UninstallFilesDir={app}\{#SubDir}
UninstallDisplayName=Hitman 2 Enhanced
; No elevation: Steam libraries are writable by the user (Steam updates games without UAC). An elevated setup also
; opened behind other windows (owner test 2026-11-11). A non-writable folder is reported on the folder page.
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=Hitman2Enhanced-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
; Branded welcome page (artwork from installer/make_art.ps1: plain shapes and text, no game assets).
DisableWelcomePage=no
WizardImageFile=art\wizard-164x314.bmp,art\wizard-328x628.bmp
WizardSmallImageFile=art\small-55.bmp,art\small-110.bmp
WizardImageStretch=no
WizardImageBackColor=$100E0E

; The language follows the Windows UI language; the selection dialog appears only when no language matches.
ShowLanguageDialog=auto
LanguageDetectionMethod=uilanguage

[Languages]
; InfoBefore page: what gets fixed, how, which files are added, how to uninstall (transparency).
; Inno Setup ships these message files; ChineseSimplified/ChineseTraditional/Hindi come from the Inno Setup
; translations repository (installer/languages/, see README there) and are used when present.
Name: "en"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "info-en.txt"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"; InfoBeforeFile: "info-es.txt"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"; InfoBeforeFile: "info-de.txt"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"; InfoBeforeFile: "info-fr.txt"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"; InfoBeforeFile: "info-ru.txt"
Name: "ar"; MessagesFile: "compiler:Languages\Arabic.isl"; InfoBeforeFile: "info-ar.txt"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"; InfoBeforeFile: "info-ja.txt"
#if FileExists(AddBackslash(SourcePath) + "languages\ChineseSimplified.isl")
  #define HaveZhCn
Name: "zhcn"; MessagesFile: "languages\ChineseSimplified.isl"; InfoBeforeFile: "info-zhcn.txt"
#endif
#if FileExists(AddBackslash(SourcePath) + "languages\ChineseTraditional.isl")
  #define HaveZhTw
Name: "zhtw"; MessagesFile: "languages\ChineseTraditional.isl"; InfoBeforeFile: "info-zhtw.txt"
#endif
#if FileExists(AddBackslash(SourcePath) + "languages\Hindi.islu")
  #define HaveHi
Name: "hi"; MessagesFile: "languages\Hindi.islu"; InfoBeforeFile: "info-hi.txt"
#endif

[Tasks]
Name: "resolution"; Description: "{cm:TaskResolution}"; GroupDescription: "{cm:GroupFixes}"
Name: "menuscale"; Description: "{cm:TaskMenuScale}"; GroupDescription: "{cm:GroupFixes}"
Name: "framelimit"; Description: "{cm:TaskFrameLimit}"; GroupDescription: "{cm:GroupFixes}"
Name: "timer"; Description: "{cm:TaskTimer}"; GroupDescription: "{cm:GroupFixes}"
Name: "settings"; Description: "{cm:TaskSettings}"; GroupDescription: "{cm:GroupFixes}"
Name: "hdfonts"; Description: "{cm:TaskHdFonts}"; GroupDescription: "{cm:GroupFixes}"

; Texts of each language (custom messages + the standard messages we override).
#include "lang\en.iss"
#include "lang\es.iss"
#include "lang\de.iss"
#include "lang\fr.iss"
#include "lang\ru.iss"
#include "lang\ar.iss"
#include "lang\ja.iss"
#ifdef HaveZhCn
  #include "lang\zhcn.iss"
#endif
#ifdef HaveZhTw
  #include "lang\zhtw.iss"
#endif
#ifdef HaveHi
  #include "lang\hi.iss"
#endif

[Files]
Source: "{#PayloadDir}\d3d8.dll"; DestDir: "{app}"; Flags: ignoreversion
; Keep the player's configuration on update.
Source: "{#PayloadDir}\h2e.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "{#PayloadDir}\README.txt"; DestDir: "{app}\{#SubDir}"; Flags: ignoreversion
Source: "{#PayloadDir}\CHANGELOG.md"; DestDir: "{app}\{#SubDir}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}\{#SubDir}"; DestName: "LICENSE.txt"; Flags: ignoreversion
; tools/hdfonts_build.py frozen with PyInstaller: re-renders the font pages from the player's own game files.
Source: "{#PayloadDir}\hdfonts_build.exe"; DestDir: "{app}\{#SubDir}"; Flags: ignoreversion; Tasks: hdfonts

[Run]
; Through cmd so the builder's output is kept in hdfonts_build.log (diagnosis when the pack is missing).
Filename: "{cmd}"; Parameters: "/C """"{app}\{#SubDir}\hdfonts_build.exe"" ""{app}"" --out ""{app}\h2e_hdfonts.bin"" > ""{app}\{#SubDir}\hdfonts_build.log"" 2>&1"""; WorkingDir: "{app}\{#SubDir}"; StatusMsg: "{cm:BuildingFonts}"; Flags: runhidden waituntilterminated; Tasks: hdfonts

[UninstallDelete]
; Files the proxy and the font builder write.
Type: files; Name: "{app}\h2e.ini"
Type: files; Name: "{app}\h2e.log"
Type: files; Name: "{app}\h2e_settings.ini"
Type: files; Name: "{app}\h2e_hdfonts.bin"
Type: files; Name: "{app}\{#SubDir}\hdfonts_build.log"
Type: dirifempty; Name: "{app}\{#SubDir}"

[Code]
const
  GameExe = 'hitman2.exe';
  SteamAppKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 6850';
  // hitman2.exe builds with all fixes (src/fixes/game_build.cpp): Steam 1.0.0.277, retail 1.0.0.265.
  KnownExeSha256 = 'f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136';
  KnownExeSha256Retail = '0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560';
  ProxyMarker = 'Hitman 2 Enhanced';

function IsGameDir(const Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + GameExe);
end;

// Steam registers every installed game under "Steam App <id>" (32-bit view); otherwise try the default library.
function DefaultGameDir(Param: String): String;
var
  Dir, Steam: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM32, SteamAppKey, 'InstallLocation', Dir) and IsGameDir(Dir) then
    Result := Dir
  else if IsWin64 and RegQueryStringValue(HKLM64, SteamAppKey, 'InstallLocation', Dir) and IsGameDir(Dir) then
    Result := Dir
  else begin
    Dir := '';
    Steam := '';
    if not RegQueryStringValue(HKLM32, 'SOFTWARE\Valve\Steam', 'InstallPath', Steam) then
      if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', Steam) then
        StringChangeEx(Steam, '/', '\', True);
    if Steam <> '' then
      Dir := AddBackslash(Steam) + 'steamapps\common\Hitman 2 Silent Assassin';
    if IsGameDir(Dir) then
      Result := Dir;
  end;
  if Result = '' then
    Result := ExpandConstant('{commonpf32}\Steam\steamapps\common\Hitman 2 Silent Assassin');
end;

// A d3d8.dll of ours contains the version string "Hitman 2 Enhanced x.y.z".
function IsOurProxy(const FileName: String): Boolean;
var
  Data: AnsiString;
begin
  Result := LoadStringFromFile(FileName, Data) and (Pos(ProxyMarker, Data) > 0);
end;

// The installer runs without administrator rights: check that the game folder accepts new files.
function IsWritableDir(const Dir: String): Boolean;
var
  Probe: String;
begin
  Probe := Dir + 'h2e-write-test.tmp';
  Result := SaveStringToFile(Probe, 'x', False);
  if Result then
    DeleteFile(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, Hash: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    exit;
  Dir := AddBackslash(WizardDirValue);
  if not IsGameDir(Dir) then begin
    MsgBox(CustomMessage('NoGame'), mbError, MB_OK);
    Result := False;
    exit;
  end;
  if FileExists(Dir + 'd3d8.dll') and not IsOurProxy(Dir + 'd3d8.dll') then begin
    MsgBox(CustomMessage('ForeignDll'), mbError, MB_OK);
    Result := False;
    exit;
  end;
  if not IsWritableDir(Dir) then begin
    MsgBox(CustomMessage('NotWritable'), mbError, MB_OK);
    Result := False;
    exit;
  end;
  Hash := GetSHA256OfFile(Dir + GameExe);
  if (Hash <> KnownExeSha256) and (Hash <> KnownExeSha256Retail) then
    Result := MsgBox(CustomMessage('UnknownExe'), mbConfirmation, MB_YESNO) = IDYES;
end;

function SetForegroundWindow(Wnd: HWND): BOOL; external 'SetForegroundWindow@user32.dll stdcall';

procedure RepoLinkClick(Sender: TObject);
var
  Code: Integer;
begin
  ShellExecAsOriginalUser('open', '{#RepoUrl}', '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;

// Link to the project page at the bottom left of every wizard page.
procedure InitializeWizard;
var
  Link: TNewStaticText;
begin
  Link := TNewStaticText.Create(WizardForm);
  Link.Parent := WizardForm;
  Link.Caption := '{#RepoUrlShort}';
  Link.Hint := CustomMessage('ProjectPage') + ' {#RepoUrl}';
  Link.ShowHint := True;
  Link.Cursor := crHand;
  Link.Font.Color := clHotLight;
  Link.Font.Style := [fsUnderline];
  Link.OnClick := @RepoLinkClick;
  Link.Left := ScaleX(16);
  Link.Top := WizardForm.CancelButton.Top + (WizardForm.CancelButton.Height - Link.Height) div 2;
end;

// The first page brings the window to the front (it could open behind other windows).
var
  BroughtToFront: Boolean;

procedure CurPageChanged(CurPageID: Integer);
begin
  if not BroughtToFront then begin
    BroughtToFront := True;
    BringToFrontAndRestore;
    SetForegroundWindow(WizardForm.Handle);
  end;
end;

// h2e.ini: [<fix>] Enabled follows the task choices (also on update, where the rest of the file is kept).
procedure SetFix(const Fix: String);
var
  Value: String;
begin
  if WizardIsTaskSelected(Fix) then
    Value := '1'
  else
    Value := '0';
  SetIniString(Fix, 'Enabled', Value, ExpandConstant('{app}\h2e.ini'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    SetFix('resolution');
    SetFix('menuscale');
    SetFix('framelimit');
    SetFix('timer');
    SetFix('settings');
    SetFix('hdfonts');
  end;
end;

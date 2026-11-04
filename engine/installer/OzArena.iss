; Oz Arena's Windows installer (Inno Setup 6). engine/tools/make_installer.py compiles it, with the
; staged build's file list in FilesInc and these defines on ISCC's command line:
;   Stage        the staged build folder (build/engine)
;   Version      OZ_VERSION as the engine prints it (0.7.0-beta)
;   VersionNum   its numbers for Windows' version info (0.7.0.0)
;   AppId        the install's identity in the registry (OzArena; tests use their own, so they never touch the game's)
;   FilesInc     the generated [Files] section
;   Keep         the paks and renderer modules this version ships, ';'-separated: older Oz files are removed
;   Icon         the game's icon
;   Compression  Inno Setup's Compression setting (lzma2/normal, or none for a quick test build)
;
; Where it installs: by default a folder of its own inside the player's Quake III Arena folder, so Oz's
; files never mix with id's or a Steam verify's. The game is found as the engine finds it (win_shared.c
; Sys_SteamPath): Steam's record of the game, every Steam library in libraryfolders.vdf, GOG's record,
; then a few plain folders. Without one, {autopf}\Oz Arena, and the player is told the game needs a copy.
; A folder Setup can't write (Program Files) makes it start again elevated, with the same choices.

#ifndef Stage
  #error Run engine/tools/make_installer.py
#endif
#define AppName "Oz Arena"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#Version}
AppVerName={#AppName} {#Version}
AppPublisher=Ozmium
AppPublisherURL=https://github.com/ozmiumofficial/oz-arena
AppSupportURL=https://github.com/ozmiumofficial/oz-arena/issues
AppUpdatesURL=https://github.com/ozmiumofficial/oz-arena/releases
VersionInfoVersion={#VersionNum}
VersionInfoDescription={#AppName} Setup
DefaultDirName={code:DefaultDir}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=no
DirExistsWarning=no
UsePreviousAppDir=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile={#Icon}
UninstallDisplayIcon={app}\OzArena.exe
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression={#Compression}
SolidCompression=no
#if Compression != "none"
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4
#endif
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
BeveledLabel={#AppName} {#Version}

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "urlscheme"; Description: "Open ozarena:// links (a friend's invite, a server link) in {#AppName}"; GroupDescription: "Links:"

[Files]
#include FilesInc

[Dirs]
; map packs go here (the engine's fs_mapspath); removed with the game while still empty
Name: "{app}\Maps"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\OzArena.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\OzArena.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\ozarena"; ValueType: string; ValueName: ""; ValueData: "URL:{#AppName} link"; Flags: uninsdeletekey; Tasks: urlscheme
Root: HKA; Subkey: "Software\Classes\ozarena"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Tasks: urlscheme
Root: HKA; Subkey: "Software\Classes\ozarena\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\OzArena.exe,0"; Tasks: urlscheme
Root: HKA; Subkey: "Software\Classes\ozarena\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OzArena.exe"" +oz_link ""%1"""; Tasks: urlscheme

[Run]
Filename: "{app}\OzArena.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
const
  Q3Name = 'Quake III Arena';

var
  Q3Dir: String;    // the player's Quake III Arena folder, '' when none was found
  Q3How: String;    // where it was found

procedure ExitProcess(uExitCode: UINT); external 'ExitProcess@kernel32.dll stdcall';

function HasBaseq3(const Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + 'baseq3\pak0.pk3');
end;

function TidyDir(S: String): String;
begin
  StringChangeEx(S, '/', '\', True);
  Result := RemoveBackslashUnlessRoot(Trim(S));
end;

{ the game in a Steam library: Steam's own folder, then every library in steamapps\libraryfolders.vdf
  ("path" "C:\\steam" lines; the folder name is Steam's, "Quake 3 Arena") }
function SteamLibraryGame(const SteamRoot: String; var Dir: String): Boolean;
var
  Lines: TArrayOfString;
  Line, Value: String;
  i: Integer;
begin
  Result := False;
  Dir := AddBackslash(SteamRoot) + 'steamapps\common\Quake 3 Arena';
  if HasBaseq3(Dir) then begin
    Result := True;
    exit;
  end;
  if not LoadStringsFromFile(AddBackslash(SteamRoot) + 'steamapps\libraryfolders.vdf', Lines) then
    exit;
  for i := 0 to GetArrayLength(Lines) - 1 do begin
    Line := Trim(Lines[i]);
    if Pos('"path"', Line) = 1 then begin
      Value := Trim(Copy(Line, 7, Length(Line)));
      if (Length(Value) >= 2) and (Value[1] = '"') and (Value[Length(Value)] = '"') then
        Value := Copy(Value, 2, Length(Value) - 2);
      StringChangeEx(Value, '\\', '\', True);
      Dir := AddBackslash(TidyDir(Value)) + 'steamapps\common\Quake 3 Arena';
      if HasBaseq3(Dir) then begin
        Result := True;
        exit;
      end;
    end;
  end;
end;

{ the player's Quake III Arena, as the engine looks for it (win_shared.c Sys_SteamPath) }
function FindQ3: Boolean;
var
  S, D: String;
  Plain: TArrayOfString;
  i: Integer;
begin
  Result := True;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 2200', 'InstallLocation', S) then begin
    S := TidyDir(S);
    if HasBaseq3(S) then begin
      Q3Dir := S;
      Q3How := 'Steam';
      exit;
    end;
  end;
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', S) or
     RegQueryStringValue(HKLM32, 'SOFTWARE\Valve\Steam', 'InstallPath', S) then begin
    if SteamLibraryGame(TidyDir(S), D) then begin
      Q3Dir := D;
      Q3How := 'Steam library';
      exit;
    end;
  end;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\GOG.com\Games\1441704920', 'path', S) then begin
    S := TidyDir(S);
    if HasBaseq3(S) then begin
      Q3Dir := S;
      Q3How := 'GOG';
      exit;
    end;
  end;
  SetArrayLength(Plain, 5);
  Plain[0] := ExpandConstant('{commonpf32}\' + Q3Name);
  Plain[1] := ExpandConstant('{commonpf32}\GOG Galaxy\Games\' + Q3Name);
  Plain[2] := ExpandConstant('{commonpf32}\GOG.com\' + Q3Name);
  Plain[3] := ExpandConstant('{sd}\' + Q3Name);
  Plain[4] := ExpandConstant('{sd}\Games\' + Q3Name);
  for i := 0 to GetArrayLength(Plain) - 1 do begin
    if HasBaseq3(Plain[i]) then begin
      Q3Dir := Plain[i];
      Q3How := 'a folder';
      exit;
    end;
  end;
  Result := False;
end;

function DefaultDir(Param: String): String;
begin
  if Q3Dir <> '' then
    Result := AddBackslash(Q3Dir) + '{#AppName}'
  else
    Result := ExpandConstant('{autopf}\{#AppName}');
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  if FindQ3 then
    Log('Oz: ' + Q3Name + ' found at ' + Q3Dir + ' (' + Q3How + ')')
  else
    Log('Oz: ' + Q3Name + ' not found');
  Log('Oz: default folder ' + DefaultDir(''));
end;

procedure InitializeWizard;
begin
  if Q3Dir = '' then
    CreateOutputMsgPage(wpWelcome, Q3Name,
      '{#AppName} needs your own copy of ' + Q3Name + '.',
      'Setup didn''t find one on this PC: not in your Steam libraries, among your GOG games, or in the usual folders.' + #13#10#13#10 +
      'You can install {#AppName} now. Before you play, put your copy''s baseq3 folder (pak0.pk3 to pak8.pk3) ' +
      'next to OzArena.exe, or install {#AppName} inside your ' + Q3Name + ' folder.' + #13#10#13#10 +
      'Setup never copies the game''s own files.');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then begin
    if Q3Dir <> '' then
      WizardForm.SelectDirLabel.Caption :=
        'Setup found your ' + Q3Name + ' in ' + Q3Dir + '. {#AppName} goes in a folder of its own beside it, ' +
        'so its files never mix with the game''s.'
    else
      WizardForm.SelectDirLabel.Caption :=
        'Setup didn''t find ' + Q3Name + ' on this PC. Install {#AppName} anywhere; it reads the game''s baseq3 ' +
        'from the folder above it, or from a copy next to OzArena.exe.';
  end;
end;

{ can Setup write there? Tested in the nearest folder that exists, so nothing is left behind }
function DirWritable(Dir: String): Boolean;
var
  F: String;
begin
  Result := False;
  Dir := RemoveBackslashUnlessRoot(Dir);
  while (Dir <> '') and not DirExists(Dir) do begin
    if ExtractFileDir(Dir) = Dir then
      exit;
    Dir := ExtractFileDir(Dir);
  end;
  if Dir = '' then
    exit;
  F := AddBackslash(Dir) + '~oz_setup_' + IntToStr(Random(1000000)) + '.tmp';
  if SaveStringToFile(F, 'x', False) then begin
    DeleteFile(F);
    Result := True;
  end;
end;

{ the folder needs administrator rights: Setup starts again elevated, with the choices made here }
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, Params: String;
  Code: Integer;
begin
  Result := True;
  if (CurPageID = wpReady) and not IsAdminInstallMode then begin
    Dir := WizardDirValue;
    if DirWritable(Dir) then
      exit;
    Log('Oz: ' + Dir + ' needs administrator rights: starting again elevated');
    Params := '/ALLUSERS /SILENT /DIR="' + Dir + '" /TASKS="' + WizardSelectedTasks(False) + '"';
    if ShellExec('runas', ExpandConstant('{srcexe}'), Params, '', SW_SHOWNORMAL, ewNoWait, Code) then
      ExitProcess(0);
    MsgBox('Setup needs administrator rights to install into ' + Dir + '. Pick another folder, or allow it when Windows asks.', mbError, MB_OK);
    Result := False;
  end;
end;

{ an older version's Oz files that this version no longer ships: its paks (pak_oz_*, zz-oz-*) and renderer
  modules not in Keep. The player's own files (map packs, configs) are never touched. }
procedure RemoveObsolete(const Dir, Pattern, Prefix1, Prefix2: String);
var
  F: TFindRec;
  Keep, Name: String;
begin
  Keep := ';' + Lowercase('{#Keep}') + ';';
  if FindFirst(AddBackslash(Dir) + Pattern, F) then begin
    try
      repeat
        Name := Lowercase(F.Name);
        if ((Pos(Prefix1, Name) = 1) or ((Prefix2 <> '') and (Pos(Prefix2, Name) = 1))) and (Pos(';' + Name + ';', Keep) = 0) then begin
          Log('Oz: removing ' + AddBackslash(Dir) + F.Name + ' (not in this version)');
          DeleteFile(AddBackslash(Dir) + F.Name);
        end;
      until not FindNext(F);
    finally
      FindClose(F);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then begin
    RemoveObsolete(ExpandConstant('{app}\oz'), '*.pk3', 'pak_oz_', 'zz-oz-');
    RemoveObsolete(ExpandConstant('{app}'), 'OzArena_*.dll', 'ozarena_', '');
  end;
end;

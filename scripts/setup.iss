; L 语言 Windows 安装包 Inno Setup 脚本
; 用于生成免依赖、可直接双击安装且自动配置 PATH 的独立 exe 安装程序

#define MyAppName "L 语言"
#define MyAppEnglishName "L-Language"
#define MyAppVersion "0.1.3.202611092011"
#define MyAppPublisher "L Language Project"
#define MyAppExeName "l.exe"

[Setup]
; 唯一 GUID 标识符
AppId={{D37F67B5-8F21-4E83-9990-AEB9C187B576}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\lang
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=lang-setup-windows-x86_64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UninstallDisplayIcon={app}\{#MyAppExeName}
ChangesEnvironment=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "将 L 语言添加到当前用户的 PATH 环境变量 (推荐，安装后可全局运行 'l')"; Flags: checkedonce

[Files]
Source: "..\l.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\stdlib\*"; DestDir: "{app}\stdlib"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\INSTALL.md"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}'))

[Code]
// 检查当前 PATH 中是否已有该目录，避免重复添加
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + UpperCase(Param) + ';', ';' + UpperCase(OrigPath) + ';') = 0;
end;

// 卸载后自动从 PATH 中移除安装路径
procedure RemovePath(Param: string);
var
  OrigPath: string;
  P: Integer;
begin
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    P := Pos(';' + UpperCase(Param) + ';', ';' + UpperCase(OrigPath) + ';');
    if P > 0 then
    begin
      Delete(OrigPath, P, Length(Param) + 1);
      if (Length(OrigPath) > 0) and (OrigPath[1] = ';') then
        Delete(OrigPath, 1, 1);
      RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemovePath(ExpandConstant('{app}'));
  end;
end;

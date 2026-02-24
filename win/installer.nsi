; =============================================================================
; はじむ言語 Windows インストーラー
; NSIS (Nullsoft Scriptable Install System) スクリプト
;
; ビルド方法:
;   makensis -NOCD win/installer.nsi
; または:
;   make windows-installer
; =============================================================================

; ── モダン UI 2 を使用 ─────────────────────────────────────────
!include "MUI2.nsh"

; ── バージョン情報 ─────────────────────────────────────────────
!define APP_NAME        "はじむ言語"
!define APP_NAME_EN     "Hajimu"
!define APP_VERSION     "1.2.11"
!define APP_PUBLISHER   "Reo Shiozawa"
!define APP_URL         "https://github.com/ReoShiozawa/hajimu"
!define APP_EXE         "hajimu.exe"
!define UNINSTALLER     "Uninstall Hajimu.exe"
!define REG_ROOT        "HKLM"
!define REG_APP_PATH    "Software\Hajimu"
!define REG_UNINSTALL   "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hajimu"

; ── インストーラー基本設定 ─────────────────────────────────────
Name              "${APP_NAME} ${APP_VERSION}"
OutFile           "win\dist\hajimu_setup.exe"
InstallDir        "$PROGRAMFILES64\${APP_NAME_EN}"
InstallDirRegKey  ${REG_ROOT} "${REG_APP_PATH}" "InstallDir"
RequestExecutionLevel admin    ; PATH はシステム全体に設定するため管理者権限が必要
Unicode True                   ; 日本語UIのためUnicode必須

; ── MUI2 設定 ─────────────────────────────────────────────────
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE   "${APP_NAME} ${APP_VERSION} セットアップ"
!define MUI_WELCOMEPAGE_TEXT    "はじむは日本語で書けるプログラミング言語です。$\r$\n$\r$\nインストール先フォルダーに hajimu.exe をインストールし、システムの PATH に自動追加します。$\r$\n$\r$\nインストール後は PowerShell / コマンドプロンプトで$\r$\n  hajimu プログラム.jp$\r$\nと実行できます。"
!define MUI_FINISHPAGE_RUN       "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT  "はじむ REPL を起動する"
!define MUI_FINISHPAGE_LINK      "はじむ公式サポートページ"
!define MUI_FINISHPAGE_LINK_LOCATION "${APP_URL}"

; ── ページ定義 ─────────────────────────────────────────────────
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; ── 言語 (日本語優先) ────────────────────────────────────────
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "English"

; =============================================================================
; インストールセクション
; =============================================================================
Section "はじむ言語 (必須)" SecMain
    SectionIn RO    ; 必須コンポーネント（選択解除不可）

    SetOutPath "$INSTDIR"

    ; ── 実行ファイル ──────────────────────────────────────────
    File "win\dist\hajimu.exe"

    ; ── 依存 DLL ─────────────────────────────────────────────
    File /nonfatal "win\dist\libcurl-x64.dll"
    File /nonfatal "win\dist\libwinpthread-1.dll"


    ; ── レジストリ: インストールパスを保存 ──────────────────
    WriteRegStr ${REG_ROOT} "${REG_APP_PATH}" "InstallDir" "$INSTDIR"
    WriteRegStr ${REG_ROOT} "${REG_APP_PATH}" "Version"    "${APP_VERSION}"

    ; ── アンインストール情報をレジストリに登録 ────────────────
    WriteRegStr ${REG_ROOT} "${REG_UNINSTALL}" "DisplayName"     "${APP_NAME} ${APP_VERSION}"
    WriteRegStr ${REG_ROOT} "${REG_UNINSTALL}" "Publisher"       "${APP_PUBLISHER}"
    WriteRegStr ${REG_ROOT} "${REG_UNINSTALL}" "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr ${REG_ROOT} "${REG_UNINSTALL}" "URLInfoAbout"    "${APP_URL}"
    WriteRegStr ${REG_ROOT} "${REG_UNINSTALL}" "InstallLocation" "$INSTDIR"
    WriteRegStr ${REG_ROOT} "${REG_UNINSTALL}" "UninstallString" \
        '"$INSTDIR\${UNINSTALLER}"'
    WriteRegDWORD ${REG_ROOT} "${REG_UNINSTALL}" "NoModify" 1
    WriteRegDWORD ${REG_ROOT} "${REG_UNINSTALL}" "NoRepair" 1

    ; ── アンインストーラーを生成 ─────────────────────────────
    WriteUninstaller "$INSTDIR\${UNINSTALLER}"

    ; ── システム PATH に追加 (PowerShell 経由) ─────────────
    ; 既に含まれている場合は追加しない
    nsExec::ExecToStack 'powershell -NoProfile -NonInteractive -Command \
        "$$p = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\"); \
         if ($$p -notlike \"*$INSTDIR*\") { \
           [Environment]::SetEnvironmentVariable(\"PATH\", $$p + \";$INSTDIR\", \"Machine\") \
         }"'
    Pop $0  ; 戻り値 (0=成功)
    ; PATH 変更を現在のプロセス環境にも反映
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; ── スタートメニューにショートカット ────────────────────
    CreateDirectory "$SMPROGRAMS\${APP_NAME_EN}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME_EN}\はじむ REPL.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
    CreateShortcut  "$SMPROGRAMS\${APP_NAME_EN}\アンインストール.lnk" \
        "$INSTDIR\${UNINSTALLER}" "" "$INSTDIR\${UNINSTALLER}" 0
SectionEnd

; =============================================================================
; アンインストールセクション
; =============================================================================
Section "Uninstall"
    ; ── ファイルの削除 ────────────────────────────────────────
    Delete "$INSTDIR\hajimu.exe"
    Delete "$INSTDIR\libcurl-x64.dll"
    Delete "$INSTDIR\libwinpthread-1.dll"
    Delete "$INSTDIR\${UNINSTALLER}"
    RMDir  "$INSTDIR"

    ; ── スタートメニューの削除 ───────────────────────────────
    Delete "$SMPROGRAMS\${APP_NAME_EN}\はじむ REPL.lnk"
    Delete "$SMPROGRAMS\${APP_NAME_EN}\アンインストール.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME_EN}"

    ; ── レジストリの削除 ─────────────────────────────────────
    DeleteRegKey ${REG_ROOT} "${REG_UNINSTALL}"
    DeleteRegKey ${REG_ROOT} "${REG_APP_PATH}"

    ; ── PATH から削除 (PowerShell 経由) ─────────────────────
    nsExec::ExecToStack 'powershell -NoProfile -NonInteractive -Command \
        "$$p = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\"); \
         $$newPath = ($$p -split \";\" | Where-Object { $$_ -ne \"$INSTDIR\" }) -join \";\"; \
         [Environment]::SetEnvironmentVariable(\"PATH\", $$newPath, \"Machine\")"'
    Pop $0
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
SectionEnd

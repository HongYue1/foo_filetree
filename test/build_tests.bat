@echo off
rem Builds and runs the offline model tests. Output: test\tests.out
rem A compile error leaves the previous tests.out in place - this script deletes it first, so a
rem stale "EXIT=0" can never be mistaken for a pass.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
if exist tests.out del tests.out
if exist out\model_test.exe del out\model_test.exe
cl /nologo /EHsc /std:c++latest /O2 /MT /W4 /WX /permissive- /DUNICODE /D_UNICODE /DNOMINMAX ^
  /Fo:out\ /Fe:out\model_test.exe model_test.cpp settings_test.cpp kinds_test.cpp library_test.cpp selection_test.cpp access_test.cpp ^
  ..\src\model\sort.cpp ..\src\model\extension_set.cpp ..\src\model\file_kind.cpp ..\src\model\library_index.cpp ..\src\model\name_pool.cpp ..\src\model\tree.cpp ..\src\model\tree_selection.cpp ..\src\model\status_text.cpp ..\src\model\filter_rules.cpp ..\src\settings\settings_model.cpp ..\src\settings\panel_state.cpp ^
  ..\src\platform\worker_pool.cpp ..\src\fs\enumerate.cpp ..\src\fs\enumeration_service.cpp ..\src\fs\drives.cpp ..\src\fs\watcher.cpp ..\src\actions\action.cpp ..\src\actions\presets.cpp ..\src\view\accessible.cpp ^
  /link /SUBSYSTEM:CONSOLE user32.lib ole32.lib oleaut32.lib oleacc.lib > out\build_tests.txt 2>&1
if errorlevel 1 (
  echo BUILD FAILED > tests.out
  type out\build_tests.txt >> tests.out
  type tests.out
  exit /b 1
)
out\model_test.exe > tests.out 2>&1
echo EXIT=%ERRORLEVEL% >> tests.out
type tests.out

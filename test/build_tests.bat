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
  /Fo:out\ /Fe:out\model_test.exe model_test.cpp settings_test.cpp ^
  ..\src\model\sort.cpp ..\src\model\extension_set.cpp ..\src\model\name_pool.cpp ..\src\model\tree.cpp ..\src\model\filter_rules.cpp ..\src\settings\settings_model.cpp ^
  ..\src\platform\worker_pool.cpp ..\src\fs\enumerate.cpp ..\src\fs\enumeration_service.cpp ..\src\fs\drives.cpp ..\src\actions\action.cpp ..\src\actions\presets.cpp ^
  /link /SUBSYSTEM:CONSOLE > out\build_tests.txt 2>&1
if errorlevel 1 (
  echo BUILD FAILED > tests.out
  type out\build_tests.txt >> tests.out
  type tests.out
  exit /b 1
)
out\model_test.exe > tests.out 2>&1
echo EXIT=%ERRORLEVEL% >> tests.out
type tests.out

@echo off
rem Package foo_filetree as dist\foo_filetree.fb2k-component. Usage: package.bat
rem
rem A .fb2k-component is a plain zip: the 32-bit DLL at the root, the 64-bit one in x64/.
rem foobar2000 for ARM (ARM64EC) loads the x64 DLL. PDBs go to dist\symbols (keep them per
rem release for crash reports; they are not shipped). Builds go through build.bat only.
setlocal
for %%P in (x64 Win32) do (
  call build.bat Release %%P
  if errorlevel 1 (
    echo FAILED: %%P build failed - see the build log
    exit /b 1
  )
)
if not exist "x64\Release\foo_filetree.dll" (echo FAILED: no x64 DLL & exit /b 1)
if not exist "Win32\Release\foo_filetree.dll" (echo FAILED: no x86 DLL & exit /b 1)

if exist dist rmdir /s /q dist
mkdir dist\stage\x64
mkdir dist\symbols
copy /y "Win32\Release\foo_filetree.dll" "dist\stage\foo_filetree.dll" >nul
copy /y "x64\Release\foo_filetree.dll" "dist\stage\x64\foo_filetree.dll" >nul
copy /y "Win32\Release\foo_filetree.pdb" "dist\symbols\foo_filetree-x86.pdb" >nul
copy /y "x64\Release\foo_filetree.pdb" "dist\symbols\foo_filetree-x64.pdb" >nul

rem 7-Zip, not Compress-Archive: Windows PowerShell 5 writes backslashes in entry names.
set SEVENZIP=C:\Program Files\7-Zip\7z.exe
if not exist "%SEVENZIP%" (echo FAILED: 7z.exe not found & exit /b 1)
pushd dist\stage
"%SEVENZIP%" a -tzip -bso0 -bsp0 "..\foo_filetree.fb2k-component" * >nul
set ZIPERR=%ERRORLEVEL%
popd
if not "%ZIPERR%"=="0" (echo FAILED: 7z exited with %ZIPERR% & exit /b 1)
rmdir /s /q dist\stage
"%SEVENZIP%" l -slt "dist\foo_filetree.fb2k-component" | findstr /b /c:"Path = "
echo Packaged dist\foo_filetree.fb2k-component; symbols in dist\symbols

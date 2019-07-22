@ECHO OFF
CALL %~dp0\InitializeEnvironment.bat || EXIT /b 10

IF "%1"=="" (SET "Configuration=Debug") ELSE (SET "Configuration=%1")

SETLOCAL
SET PATH=C:\Program Files\GVFS;C:\Program Files\Git\cmd;%PATH%

if not "%2"=="--test-gvfs-on-path" goto :startFunctionalTests

REM Force GVFS.FunctionalTests.exe to use the installed version of GVFS
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GitHooksLoader.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.CommandHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.ReadObjectHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.VirtualFileSystemHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.PostIndexChangedHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.Mount.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.Service.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.Service.UI.exe

REM Same for GVFS.FunctionalTests.Windows.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GitHooksLoader.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.CommandHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.ReadObjectHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.VirtualFileSystemHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.PostIndexChangedHook.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.Mount.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.Service.exe
del %VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.Service.UI.exe

echo PATH = %PATH%
echo gvfs location:
where gvfs
echo GVFS.Service location:
where GVFS.Service
echo git location:
where git

:startFunctionalTests
dotnet %VFS_OUTPUTDIR%\GVFS.FunctionalTests\bin\x64\%Configuration%\netcoreapp2.1\GVFS.FunctionalTests.dll /result:TestResultNetCore.xml %2 %3 %4 %5 || goto :endFunctionalTests
%VFS_OUTPUTDIR%\GVFS.FunctionalTests.Windows\bin\x64\%Configuration%\GVFS.FunctionalTests.Windows.exe /result:TestResultNetFramework.xml --windows-only %2 %3 %4 %5 || goto :endFunctionalTests

:endFunctionalTests
set error=%errorlevel%

call %VFS_SCRIPTSDIR%\StopAllServices.bat

exit /b %error%
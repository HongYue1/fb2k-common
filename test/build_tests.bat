@echo off
rem Builds and runs the fb2k-common tests. Output: test\tests.out
rem   colour_test: synthetic covers, APCA references, accent/fill rules
rem   golden_test: real covers from test\local\covers.txt against test\local\golden.txt
rem               (skipped without the list; "build_tests.bat --update" rewrites the golden file)
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
set CL_FLAGS=/nologo /EHsc /std:c++latest /O2 /MT /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /I..\include /Fo:out\
cl %CL_FLAGS% /Fe:out\colour_test.exe colour_test.cpp ..\src\cover_accent.cpp /link /SUBSYSTEM:CONSOLE > out\build_colour.txt 2>&1
if errorlevel 1 (type out\build_colour.txt & exit /b 1)
cl %CL_FLAGS% /Fe:out\golden_test.exe golden_test.cpp ..\src\cover_accent.cpp /link /SUBSYSTEM:CONSOLE > out\build_golden.txt 2>&1
if errorlevel 1 (type out\build_golden.txt & exit /b 1)
echo == colour == > tests.out
out\colour_test.exe >> tests.out 2>&1
set E1=%ERRORLEVEL%
echo == golden == >> tests.out
out\golden_test.exe %1 >> tests.out 2>&1
set E2=%ERRORLEVEL%
echo EXIT=%E1% %E2% >> tests.out
type tests.out

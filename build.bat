@echo off
echo Building Photo Viewer...

REM Create dist directory if it doesn't exist
if not exist "dist" mkdir dist

REM Compile with static linking
C:\mingw64\bin\g++.exe -o dist/PhotoViewer.exe main.cpp -mwindows

if %ERRORLEVEL% EQU 0 (
    echo Build successful! Distribution package created in 'dist' folder.
    echo You can now share the 'dist' folder with others.
) else (
    echo Build failed! Please check the error messages above.
)

pause

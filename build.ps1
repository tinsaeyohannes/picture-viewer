Write-Host "Building Photo Viewer..."

# Create dist directory if it doesn't exist
if (-not (Test-Path "dist")) {
    New-Item -ItemType Directory -Path "dist"
}

# Add MinGW to PATH
$env:Path += ";C:\mingw64\bin"

# Compile with static linking
g++ -o dist/PhotoViewer.exe main.cpp -lgdiplus -lcomctl32 -mwindows -static -static-libgcc -static-libstdc++

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build successful! Distribution package created in 'dist' folder."
    Write-Host "You can now share the 'dist' folder with others."
} else {
    Write-Host "Build failed! Please check the error messages above."
}

Read-Host "Press Enter to continue..."

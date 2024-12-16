# Photo Viewer

A simple photo viewer and editor application built with C++ and Qt6.

## Features

- Open and view images (supports PNG, JPG, BMP formats)
- Save edited images
- Zoom in/out
- Rotate images left/right
- Basic image adjustments

## Prerequisites

- CMake (version 3.16 or higher)
- Qt6
- C++ compiler with C++17 support

## Building the Application

1. Create a build directory:
```bash
mkdir build
cd build
```

2. Generate build files with CMake:
```bash
cmake ..
```

3. Build the application:
```bash
cmake --build .
```

## Usage

After building, run the application and use the menu options to:
- Open images using File > Open (Ctrl+O)
- Save images using File > Save (Ctrl+S)
- Zoom using View > Zoom In/Out (Ctrl+/Ctrl-)
- Rotate using Edit > Rotate Left/Right (Ctrl+L/Ctrl+R)

## License

This project is open source and available under the MIT License.

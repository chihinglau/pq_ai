@echo off
setlocal

echo Building PQ AI Terminal for Windows...

mkdir build-win 2>nul
cd build-win

cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo CMake configuration failed!
    exit /b 1
)

mingw32-make -j4
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo Build completed successfully!
echo Executable: build-win\sim\pq_ai_sim.exe

cd ..

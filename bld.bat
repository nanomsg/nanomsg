cd /d %~dp0
mkdir build
cd /d build
rm -rf *
cmake -G "Visual Studio 12" ..\
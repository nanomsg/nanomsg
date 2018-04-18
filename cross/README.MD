# Cross-compilation

If cross-compiling for a different architecture (e.g. arm), you have 
to add some extra parameters to the cmake call.

As of commit ee032fed911, a toolchain file `arm-linux-gnueabihf.cmake` exists in the root directory.
It contains compiler information for the GCC ARM Linux compiler and has to be passed to CMake in the build
configuration stage. In addition, a different install directory has to be provided so that the arm compiler can
find the installed libraries/headers.

1.  `% mkdir build`
2.  `% cd build`
3.  `% cmake -DCMAKE_TOOLCHAIN_FILE=../cross/arm-linux-gnueabihf.cmake \
             -DCMAKE_INSTALL_PREFIX=/usr/arm-linux-gnueabihf ..`    
4.  `% cmake --build .`
5.  `% ctest .`
6.  `% sudo cmake --build . --target install`
7.  `% sudo ldconfig` (if on Linux)
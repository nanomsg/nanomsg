#!/bin/bash

#
# common build & test steps for CircleCI jobs
#

uname -a
cmake --version
ninja --version

mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Debug} -DNN_ENABLE_COVERAGE=${COVERAGE:-OFF} ..
ninja
env CTEST_OUTPUT_ON_FAILURE=1 ninja test

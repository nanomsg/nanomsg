cmake_minimum_required (VERSION 2.8.7)

project(nanomsg-demo)

# cmake -Dnanomsg_DIR="path/to/nanomsg/lib/cmake/nanomsg" ...
find_package(nanomsg CONFIG REQUIRED)

function (nn_add_demo demo_name)
    add_executable(${demo_name} ${demo_name}.c)
    target_link_libraries(${demo_name} nanomsg::nanomsg)
endfunction (nn_add_demo)

nn_add_demo (async_demo)
nn_add_demo (device_demo)
nn_add_demo (pthread_demo)
nn_add_demo (pubsub_demo)
nn_add_demo (rpc_demo)

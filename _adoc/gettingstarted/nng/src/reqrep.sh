./reqrep node0 ipc:///tmp/reqrep.ipc & node0=$! && sleep 1
./reqrep node1 ipc:///tmp/reqrep.ipc
kill $node0

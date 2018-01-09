./bus node0 ipc:///tmp/node0.ipc ipc:///tmp/node1.ipc ipc:///tmp/node2.ipc & node0=$!
./bus node1 ipc:///tmp/node1.ipc ipc:///tmp/node2.ipc ipc:///tmp/node3.ipc & node1=$!
./bus node2 ipc:///tmp/node2.ipc ipc:///tmp/node3.ipc & node2=$!
./bus node3 ipc:///tmp/node3.ipc ipc:///tmp/node0.ipc & node3=$!
sleep 5
kill $node0 $node1 $node2 $node3

./pair node0 ipc:///tmp/pair.ipc & node0=$!
./pair node1 ipc:///tmp/pair.ipc & node1=$!
sleep 3
kill $node0 $node1


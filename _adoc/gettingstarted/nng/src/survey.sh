./survey server ipc:///tmp/survey.ipc & server=$!
./survey client ipc:///tmp/survey.ipc client0 & client0=$!
./survey client ipc:///tmp/survey.ipc client1 & client1=$!
./survey client ipc:///tmp/survey.ipc client2 & client2=$!
sleep 3 # <1>
kill $server $client0 $client1 $client2

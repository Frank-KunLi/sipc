#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib/i386-linux-gnu/:/home/sunil/MyProjects/nanomsg/ipc_poc:

			#name, count, sleepTime, cbSubModule, cbSubFunc, cbPubFunc
./test_sipc test_server_1 250 3 test_client_1 test_client_1_func test_server_1_func & server1=$! && sleep 1
./test_sipc test_client_1 100 3 test_server_1 test_server_1_func test_client_1_func & client1=$! && sleep 1
./test_sipc test_client_2 200 3 test_server_1 test_server_1_func test_client_2_func & client2=$! && sleep 1
./test_sipc test_client_3 50  1 test_server_1 test_server_1_func test_client_3_func & client3=$! && sleep 1
./test_sipc test_client_4 100 2 test_server_1 test_server_1_func test_client_4_func & client4=$! && sleep 1


#kill $server1 $client1 $client2 $client3 $client4


./test_sipc test_server_1 10 3 test_client_1 test_client_1_func test_server_1_func & server1=$! && sleep 1
./test_sipc test_client_1 10 3 test_server_1 test_server_1_func test_client_1_func & client1=$! && sleep 1

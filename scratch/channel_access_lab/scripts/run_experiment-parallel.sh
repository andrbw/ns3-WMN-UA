#!/bin/bash

set -e

mkdir -p results/noack results/ack

parallel "./../../../ns3 run --no-build --cwd=$PWD \"channel_access_lab --RngRun={1} --numOfStations={2} --protocol=aloha --useAck={3} --collectPcap=false --outFileName=results/{4}/pure-{1}-{2}.txt\"" ::: $(seq 1 5) ::: $(seq 1 10 100) ::: false true :::+ noack ack
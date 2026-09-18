#!/bin/bash

set -e

mkdir -p results/noack results/ack

modes=(false true)
dirs=(noack ack)

for i in ${!modes[@]}; do
  for run in $(seq 1 5); do
    for n in $(seq 1 10 100); do
      ./../../../ns3 run --no-build --cwd=$PWD "channel_access_lab --RngRun=$run --numOfStations=$n --protocol=aloha --useAck=${modes[$i]} --collectPcap=false --outFileName=results/${dirs[$i]}/pure-$run-$n.txt"
    done
  done
done

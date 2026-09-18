#!/bin/bash

set -e

for mode in noack ack; do
  for run in $(seq 1 5); do
    touch aloha-pure-$mode-$run.dat
    for n in $(seq 1 10 100); do
      cat results/$mode/pure-$run-$n.txt >> aloha-pure-$mode-$run.dat
    done
  done
done

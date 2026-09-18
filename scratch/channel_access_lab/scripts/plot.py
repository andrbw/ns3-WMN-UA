#!/usr/bin/python3

import numpy as np
import matplotlib.pyplot as plt

num_runs = 5
max_stas = 100

num_stas = np.array (range(1, max_stas + 1, 10))

noack = np.zeros (10)
for run in range (1, num_runs+1):
  df_noack = np.genfromtxt (f"aloha-pure-noack-{run}.dat", names=None)
  noack += df_noack[:, 1] / num_runs

ack = np.zeros (10)
for run in range (1, num_runs+1):
  df_ack = np.genfromtxt (f"aloha-pure-ack-{run}.dat", names=None)
  ack += df_ack[:, 1] / num_runs

plt.figure (figsize=[5.5, 4.0])

plt.plot (num_stas, noack, label='sim pure Aloha, without ACK', color = 'g', marker = '+', linestyle='None')
plt.plot (num_stas, ack, label='sim pure Aloha, with ACK', color = 'b', marker = 'x', linestyle='None')

plt.xlabel ("Number of stations")
plt.ylabel ("Throughput, Mbps")
plt.grid ()
plt.legend (loc="best")

plt.savefig ("throughput.png", dpi=200)

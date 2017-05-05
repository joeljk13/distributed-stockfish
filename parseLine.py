#!/usr/bin/env python
# Parses lines of Stockfish output text and returns the depth, followed by the
# time in milliseconds it took to reach that depth, for each line (if line
# contains both depth and time)

import sys

for line in sys.stdin:
    if 'depth' in line and 'time' in line:
        sp = line.strip().split()
        depth = ''
        time = ''
        for i in range(len(sp)):
            if sp[i] == 'depth':
                depth = sp[i+1]
            elif sp[i] == 'time':
                time = sp[i+1]
        if depth and time:
            print depth + " " + time



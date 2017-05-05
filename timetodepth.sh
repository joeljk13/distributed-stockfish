#!/bin/bash
# Output time it takes Stockfish to reach a certain depth.

SECS=60
BRANCHES="naive-tt irreversible-tt cache-tt cache-lock-tt global-smp"

MILLISECONDS=$(expr $SECS \* 1000)

echo "time-to-depth testing started" >&2

	for branch in $BRANCHES; do
		echo $branch
		# git checkout
		git checkout $branch
		# compile
		cd src
		make clean
		make build debug=no
		cd ..

		# run stockfish, need to keep pipe open
		for i in `seq 1 4`; do
			SECS=60
			MILLISECONDS=$(expr $SECS \* 1000)
			echo $branch $MILLISECONDS $i $SECS
			(cat <<- EOF
				setoption name Threads value $i
				go movetime $MILLISECONDS
			EOF
			sleep $SECS) | mpirun -np 4 -machinefile src/testhosts ./src/stockfish > "tmp-asdfasdfasdf.txt" 2> "data/${branch}_${i}.err"
			./parseLine.py < "tmp-asdfasdfasdf.txt" > "data/${branch}_${i}.txt"
			echo "finished with " $branch $i
		done
	done

		branch="stockfish-master"
		echo $branch
		# git checkout
		git checkout $branch
		# compile
		cd src
		make clean
		make build debug=no
		cd ..

		# run stockfish, need to keep pipe open
		for i in `seq 1 4`; do
			SECS=60
			MILLISECONDS=$(expr $SECS \* 1000)
			echo $branch $MILLISECONDS $i $SECS
			(cat <<- EOF
				setoption name Threads value $i
				go movetime $MILLISECONDS
			EOF
			sleep $SECS) | ./src/stockfish > "tmp-asdfasdfasdf.txt" 2> "data/${branch}_${i}.err"
			./parseLine.py < "tmp-asdfasdfasdf.txt" > "data/${branch}_${i}.txt"
			echo "finished with " $branch $i
		done



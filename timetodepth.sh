#!/bin/bash
# Output time it takes Stockfish to reach a certain depth.

SECONDS=120
BRANCHES="naive-tt irreversible-tt cache-tt"

MILLISECONDS=$(expr $SECONDS \* 1000)

echo "time-to-depth testing started" >&2
i=2
while true; do

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
		SECONDS=120
		echo $branch $MILLISECONDS $i $SECONDS
		(cat <<- EOF
			go movetime $MILLISECONDS
		EOF
		sleep $SECONDS) | mpirun -np 2 -machinefile src/testhosts ./src/stockfish > "tmp-asdfasdfasdf.txt" 2> "data/${branch}_${i}.err"
		./parseLine.py < "tmp-asdfasdfasdf.txt" > "data/${branch}_${i}.txt"
		echo "finished with " $branch
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
		SECONDS=120
		echo $branch $MILLISECONDS $i $SECONDS
		(cat <<- EOF
			go movetime $MILLISECONDS
		EOF
		sleep $SECONDS) | ./src/stockfish > "tmp-asdfasdfasdf.txt" 2> "data/${branch}_${i}.err"
		./parseLine.py < "tmp-asdfasdfasdf.txt" > "data/${branch}_${i}.txt"
		echo "finished with " $branch


	i=$(( $i + 1 ))
done

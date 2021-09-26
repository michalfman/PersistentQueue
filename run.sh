#!/bin/bash
ulimit -c unlimited
for i in 1 2 3 4
do
for j in 1 2 3 4 5 6 7 8
do
for f in 1 10 100 1000 10000
do
for s in 5 10 100 1000 10000 100000 1000000
do
for k in 1 2 3 4 5 6 7 8 9 10
do
./exe $i $j $f $k $s
done
done
done
done
done


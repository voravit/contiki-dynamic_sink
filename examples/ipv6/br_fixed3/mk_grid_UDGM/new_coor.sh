#!/bin/bash
COOR="coordinates_64.dat"
INDEX="index.txt"

REGION=(0 16 32 48)

for i in ${REGION[@]}
do
  for j in `seq 1 2`
  do
	k=$((${i}+${j}))
	X=`sed -n "${k}p" $INDEX`
	L=`sed -n "${X}p" $COOR`
	#echo $X $L
	echo $L
  done	
done

C=2
for i in ${REGION[@]}
do
  for j in `seq 1 4`
  do
	k=$((${i}+${j}+${C}))
	X=`sed -n "${k}p" $INDEX`
	L=`sed -n "${X}p" $COOR`
	#echo $X $L
	echo $L
  done	
done

P=4
for i in ${REGION[@]}
do
  for j in `seq 1 5`
  do
	k=$((${i}+${j}+${C}+${P}))
	X=`sed -n "${k}p" $INDEX`
	L=`sed -n "${X}p" $COOR`
	#echo $X $L
	echo $L
  done	
done

V=5
for i in ${REGION[@]}
do
  for j in `seq 1 5`
  do
	k=$((${i}+${j}+${C}+${P}+${V}))
	X=`sed -n "${k}p" $INDEX`
	L=`sed -n "${X}p" $COOR`
	#echo $X $L
	echo $L
  done	
done


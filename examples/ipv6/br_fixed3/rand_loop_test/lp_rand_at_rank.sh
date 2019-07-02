#!/bin/bash
OUTPUT=16
if [ -z "$1" ]; then
	SEED=123456
else
	SEED=$1
fi
if [ -z "$2" ]; then
	VALUE=128
else
	VALUE=$2
fi
if [ -z "$3" ]; then
	THRESHOLD=1
else
	THRESHOLD=$3
fi



BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed3"
cd $BASE_TEST

START=`date "+%Y%m%d"`
echo "mkdir ${START}_rand"
mkdir ${START}_rand

VALUE=128
THRESHOLD=1
SEED=(123456 654321)
for s in ${SEED[@]}
do
	echo "SEED: $s VALUE: $VALUE THRESHOLD: $THRESHOLD"
	./rand_at_rank.sh $s $VALUE $THRESHOLD
	mkdir $s
	mv at_rank_128 $s
	mv $s ${START}_rand
done


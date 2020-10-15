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



BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/mix_br_fixed"
cd $BASE_TEST

START=`date "+%Y%m%d"`
echo "mkdir ${START}_mix"
mkdir ${START}_mix

VALUE=128
THRESHOLD=1
SEED=(100000 200000 300000 400000 500000)
#SEED=(600000 700000 800000 900000 1000000)
#SEED=(100000 200000 300000 400000 500000 600000 700000 800000 900000 1000000)
for s in ${SEED[@]}
do
	echo "SEED: $s VALUE: $VALUE THRESHOLD: $THRESHOLD"
	./mix_at_etx.sh $s $VALUE $THRESHOLD
	mkdir $s
	mv at_etx_128 $s
	mv $s ${START}_mix
done


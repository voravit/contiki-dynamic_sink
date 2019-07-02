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



BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2"
cd $BASE_TEST

START=`date "+%Y%m%d"`
echo "mkdir ${START}_step"
mkdir ${START}_step

VALUE=30
THRESHOLD=1
SEED=(1100000 1200000 1300000 1400000 1500000)
for s in ${SEED[@]}
do
	echo "SEED: $s VALUE: $VALUE THRESHOLD: $THRESHOLD"
	./step_uc_nbr.sh $s $VALUE $THRESHOLD
	mkdir $s
	mv uc_nbr_30 $s
	mv $s ${START}_step
done


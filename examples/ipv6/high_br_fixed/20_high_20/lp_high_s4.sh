#!/bin/bash
OUTPUT=16
if [ -z "$1" ]; then
	SEED=123456
else
	SEED=$1
fi
echo "SEED: $SEED"

BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed"
cd $BASE_TEST

START=`date "+%Y%m%d"`
echo "mkdir ${START}_${SEED}_s4_20"
mkdir ${START}_${SEED}_s4_20

RATE=(20)
echo "### START TEST 16 NODES"
for r in ${RATE[@]}
do
	echo "# rate: $r"
	./high_s4.sh $r $SEED
done

mv s_4* ${START}_${SEED}_s4_20

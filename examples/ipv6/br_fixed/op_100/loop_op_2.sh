#!/bin/bash
OUTPUT=16
if [ -z "$1" ]; then
	SEED=123456
else
	SEED=$1
fi
echo "SEED: $SEED"

BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed"
cd $BASE_TEST

START=`date "+%Y%m%d"`
echo "mkdir ${START}_${SEED}_op"
mkdir ${START}_${SEED}_op

RATE=(2 3 4 5 6)
echo "### START TEST 16 NODES"
for r in ${RATE[@]}
do
	echo "# rate: $r"
	./exp1_op_2.sh $r $SEED
done

mv op_2* ${START}_${SEED}_op

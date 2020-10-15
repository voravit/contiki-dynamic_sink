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

echo "SEED: $SEED VALUE: $VALUE THRESHOLD: $THRESHOLD"

BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed"
cd $BASE_TEST

START=`date "+%Y%m%d"`
echo "mkdir ${START}_${SEED}_uc_nbr_40"
mkdir ${START}_${SEED}_uc_nbr_40

RATE=(20)
echo "### START TEST 16 NODES"
for r in ${RATE[@]}
do
	echo "# rate: $r"
	./high_uc_nbr.sh $r $SEED $VALUE $THRESHOLD
done

mv uc_nbr_* ${START}_${SEED}_uc_nbr_40

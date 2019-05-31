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


echo "SEED: $SEED VALUE: $VALUE"

BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed"
cd $BASE_TEST

START=`date "+%Y%m%d"`
echo "mkdir ${START}_${SEED}_ct_rx"
mkdir ${START}_${SEED}_ct_rx

RATE=(5 10 15 20 30)
echo "### START TEST 16 NODES"
for r in ${RATE[@]}
do
	echo "# rate: $r"
	./ct_rx.sh $r $SEED $VALUE
done

mv ct_rx_* ${START}_${SEED}_ct_rx

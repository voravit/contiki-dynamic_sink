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
echo "mkdir ${START}_${SEED}_at_etx"
mkdir ${START}_${SEED}_at_etx

RATE=(2 3 4 5 6)
echo "### START TEST 16 NODES"
for r in ${RATE[@]}
do
	echo "# rate: $r"
	./at_etx.sh $r $SEED $VALUE
done

mv at_etx_* ${START}_${SEED}_at_etx

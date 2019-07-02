#!/bin/bash
NONROOT=100
if [ -z "$1" ]; then
	SINKS=1
else
	SINKS=$1
fi
if [ -z "$2" ]; then
	RATE=2
else
	RATE=$2
fi
#./ring_run_loop $SINKS $SEED
echo "./ring_run_loop.sh $SINKS 100000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 100000

echo "./ring_run_loop.sh $SINKS 200000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 200000

echo "./ring_run_loop.sh $SINKS 300000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 300000

echo "./ring_run_loop.sh $SINKS 400000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 400000

echo "./ring_run_loop.sh $SINKS 500000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 500000

echo "./ring_run_loop.sh $SINKS 600000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 600000

echo "./ring_run_loop.sh $SINKS 700000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 700000

echo "./ring_run_loop.sh $SINKS 800000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 800000

echo "./ring_run_loop.sh $SINKS 900000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 900000

echo "./ring_run_loop.sh $SINKS 1000000"
/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed2/ring_run_loop.sh $SINKS 1000000


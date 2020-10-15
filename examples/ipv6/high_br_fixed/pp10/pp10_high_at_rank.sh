#!/bin/bash
if [ -z "$1" ]; then
        V=128
else
        V=$1
fi
if [ -z "$2" ]; then
        T=1
else
        T=$2
fi

#echo "./lp_high_at_rank.sh 123456"
#/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 123456 $V $T

echo "./lp_high_at_rank.sh 100000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 100000 $V $T

echo "./lp_high_at_rank.sh 200000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 200000 $V $T

echo "./lp_high_at_rank.sh 300000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 300000 $V $T

echo "./lp_high_at_rank.sh 400000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 400000 $V $T

echo "./lp_high_at_rank.sh 500000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 500000 $V $T

echo "./lp_high_at_rank.sh 600000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 600000 $V $T

echo "./lp_high_at_rank.sh 700000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 700000 $V $T

echo "./lp_high_at_rank.sh 800000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 800000 $V $T

echo "./lp_high_at_rank.sh 900000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 900000 $V $T

echo "./lp_high_at_rank.sh 1000000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_at_rank.sh 1000000 $V $T


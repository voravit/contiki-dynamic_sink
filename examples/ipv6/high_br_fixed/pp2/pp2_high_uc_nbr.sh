#!/bin/bash
if [ -z "$1" ]; then
        V=40
else
        V=$1
fi
if [ -z "$2" ]; then
        T=1
else
        T=$2
fi

#echo "./lp_high_uc_nbr.sh 123456"
#/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_uc_nbr.sh 123456 $V $T

echo "./lp_high_uc_nbr.sh 600000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_uc_nbr.sh 600000 $V $T

echo "./lp_high_uc_nbr.sh 700000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_uc_nbr.sh 700000 $V $T

echo "./lp_high_uc_nbr.sh 800000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_uc_nbr.sh 800000 $V $T

echo "./lp_high_uc_nbr.sh 900000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_uc_nbr.sh 900000 $V $T

echo "./lp_high_uc_nbr.sh 1000000"
/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed/lp_high_uc_nbr.sh 1000000 $V $T

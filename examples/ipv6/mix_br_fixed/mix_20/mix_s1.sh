#!/bin/bash

SINKS=9
CS=$((${SINKS}-1))
SEED=$1
#VALUE=$2
OUTPUT=16
SERVER="NOREPLY"
# PC
#BASE_TEST="/home/voravit/repo/contiki/examples/ipv6/test_collect"
#BASE_COOJA="/home/voravit/repo/contiki/tools/cooja"
# laptop
BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/mix_br_fixed"
BASE_COOJA="/home/voravit/repo/current/main/contiki/tools/cooja"
LOGFILE=${BASE_TEST}/${SEED}.log
cd $BASE_TEST

touch $LOGFILE

CTR=0
	cd $BASE_TEST
	#rm *wismote
	#make TARGET=wismote clean

	#cp grid_64_s${SINKS}.csc run.csc
	cp rand_cs${CS}.csc run.csc
	sed -i "s/<randomseed>123456/<randomseed>${SEED}/" run.csc
	#sed -i "s/MAX_RX_TRAFFIC 3000/MAX_RX_TRAFFIC ${RX_TRAFFIC}/" coordinator.c
        #/usr/bin/gcc -o coordinator coordinator.c
	sed -i "s/<success_ratio_rx>1\.0/<success_ratio_rx>0\.2/" run.csc
        cp -p project-conf.h.rand project-conf.h

        cd ../mix_br_sensor
        cp -p project-conf.h.rand project-conf.h
        cd ../mix_sender_cbr
        cp -p project-conf.h.rand project-conf.h
        cd ../mix_sender_poisson
        cp -p project-conf.h.rand project-conf.h
        cd ../mix_sender_vary
        cp -p project-conf.h.rand project-conf.h
        cd ../mix_br_fixed

	cp -a /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c.op1 /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c

	cd $BASE_COOJA/build
	FILE=${BASE_TEST}/run.csc

	echo ">>>>> Running SEED:$SEED" >> $LOGFILE
	echo "START:" `date "+%Y-%m-%d %H:%M:%S %N"` >> $LOGFILE
	echo "START:"
	#sudo ${BASE_TEST}/setup_route.sh 2 & >> $LOGFILE 2>&1
	sudo ${BASE_TEST}/setup_route.sh $SINKS $SERVER & 
	java -mx1024m -jar ${BASE_COOJA}/dist/cooja.jar -nogui=$FILE
	ERR=`grep "FATAL" COOJA.log | wc -l`
	FINISHED=`grep "finished" COOJA.log | wc -l`
	while [ $ERR -ge 1 ] && [ $FINISHED -ne 1 ];
	do
	  CTR=$((CTR+1))
	  echo "FATAL ERROR! $CTR" >> $LOGFILE
	  cd $BASE_COOJA/build
	  mkdir fatal${CTR}
  	  mv COOJA* fatal${CTR}
  	  mv log*txt radiolog*pcap fatal${CTR}
	  mv fatal${CTR} ${BASE_TEST}/${SEED}/.

	cd $BASE_TEST
	rm *wismote
	make TARGET=wismote clean

	  cd $BASE_COOJA/build

	  echo "RERUN:$CTR SEED:$SEED GRID:$g OF:$f MODE:$l" >> $LOGFILE
	  java -mx1024m -jar ${BASE_COOJA}/dist/cooja.jar -nogui=$FILE
	  ERR=`grep "FATAL" COOJA.log | wc -l`
	  FINISHED=`grep "finished" COOJA.log | wc -l`
	done

	echo "STOP:" `date "+%Y-%m-%d %H:%M:%S %N"` >> $LOGFILE
	sleep 2
	mkdir s_1
	mv COOJA* log*txt s_1
	mv *pcap s_1
	mv tun*txt s_1
	mv nohup.out s_1
	mv route.out s_1
	mv s_1 ${BASE_TEST}/.
	cd $BASE_TEST
	mv $LOGFILE s_1/.
	mv udp.out s_1/.
	mv coordinator.out s_1/.

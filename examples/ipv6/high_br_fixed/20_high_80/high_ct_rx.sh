#!/bin/bash

SINKS=4
CS=$((${SINKS}-1))
RATE=$1
SEED=$2
VALUE=$3
THRESHOLD=$4
OUTPUT=16
SERVER="NOREPLY"
# PC
#BASE_TEST="/home/voravit/repo/contiki/examples/ipv6/test_collect"
#BASE_COOJA="/home/voravit/repo/contiki/tools/cooja"
# laptop
BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/high_br_fixed"
BASE_COOJA="/home/voravit/repo/current/main/contiki/tools/cooja"
LOGFILE=${BASE_TEST}/${SEED}.log
cd $BASE_TEST

touch $LOGFILE

CTR=0
	cd $BASE_TEST
	#rm *wismote
	#make TARGET=wismote clean

	#cp grid_64_s${SINKS}.csc run.csc
	cp wgrid_16_cs${CS}.csc run.csc
	sed -i "s/<randomseed>123456/<randomseed>${SEED}/" run.csc
	#sed -i "s/MAX_RX_TRAFFIC 3000/MAX_RX_TRAFFIC ${RX_TRAFFIC}/" coordinator.c
        #/usr/bin/gcc -o coordinator coordinator.c
	sed -i "s/<success_ratio_rx>1\.0/<success_ratio_rx>0\.8/" run.csc
	#sed -i "s/<bound>false/<bound>true/" run.csc
	#sed -i "2117s/<bound>false/<bound>true/" run.csc
	#sed -i "2130s/<bound>false/<bound>true/" run.csc
	#sed -i "2143s/<bound>false/<bound>true/" run.csc
	#sed -i "2156s/<bound>false/<bound>true/" run.csc
        cp -p project-conf.h.ns.print project-conf.h
        sed -i "s/SINK_ADDITION 1/SINK_ADDITION 3/" project-conf.h
        cp -p coordinator.c.clean coordinator.c
        sed -i "s/RX_TH_ACT 50/RX_TH_ACT ${VALUE}/" coordinator.c
	sed -i "s/RX_TH_DEACT 15/RX_TH_DEACT 20/" coordinator.c
        sed -i "s/THRESHOLD_HIT 3/THRESHOLD_HIT ${THRESHOLD}/" coordinator.c
        /usr/bin/gcc -o coordinator coordinator.c

        cd ../high_br_sensor
        cp -p project-conf.h.ns.print project-conf.h
        sed -i "s/RATE 2/RATE ${RATE}/" project-conf.h
        sed -i "s/SINK_ADDITION 1/SINK_ADDITION 3/" project-conf.h
        cd ../high_sender
        cp -p project-conf.h.ns.print project-conf.h
        sed -i "s/RATE 2/RATE ${RATE}/" project-conf.h
        cd ../high_br_fixed
#        cp -a /home/voravit/repo/current/main/contiki/core/net/rpl.auto/* /home/voravit/repo/current/main/contiki/core/net/rpl/.
#	cp -a /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c.rank /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c
	cp -a /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c.at.rank /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c
        sed -i "s/THRESHOLD_HIT 3/THRESHOLD_HIT ${THRESHOLD}/" /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c

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
	mkdir ct_rx_${VALUE}_${RATE}
	mv COOJA* log*txt ct_rx_${VALUE}_${RATE}
	mv *pcap ct_rx_${VALUE}_${RATE}
	mv tun*txt ct_rx_${VALUE}_${RATE}
	mv nohup.out ct_rx_${VALUE}_${RATE}
	mv route.out ct_rx_${VALUE}_${RATE}
	mv ct_rx_${VALUE}_${RATE} ${BASE_TEST}/.
	cd $BASE_TEST
	mv $LOGFILE ct_rx_${VALUE}_${RATE}/.
	mv udp.out ct_rx_${VALUE}_${RATE}/.
	mv coordinator.out ct_rx_${VALUE}_${RATE}/.
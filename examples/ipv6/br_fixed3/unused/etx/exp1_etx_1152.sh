#!/bin/bash

SINKS=4
CS=$((${SINKS}-1))
RATE=$1
SEED=$2
OUTPUT=16
SERVER="NOREPLY"
# PC
#BASE_TEST="/home/voravit/repo/contiki/examples/ipv6/test_collect"
#BASE_COOJA="/home/voravit/repo/contiki/tools/cooja"
# laptop
BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed"
BASE_COOJA="/home/voravit/repo/current/main/contiki/tools/cooja"
LOGFILE=${BASE_TEST}/${SEED}.log
cd $BASE_TEST

touch $LOGFILE

CTR=0
	cd $BASE_TEST
	#rm *wismote
	#make TARGET=wismote clean

	#cp grid_64_s${SINKS}.csc run.csc
	cp grid_16_cs${CS}.csc run.csc
	sed -i "s/<randomseed>123456/<randomseed>${SEED}/" run.csc
	#sed -i "s/MAX_RX_TRAFFIC 3000/MAX_RX_TRAFFIC ${RX_TRAFFIC}/" coordinator.c
        #/usr/bin/gcc -o coordinator coordinator.c
	#sed -i "s/<success_ratio_rx>1\.0/<success_ratio_rx>0\.8/" run.csc
	#sed -i "s/<bound>false/<bound>true/" run.csc
	#sed -i "2117s/<bound>false/<bound>true/" run.csc
	#sed -i "2130s/<bound>false/<bound>true/" run.csc
	#sed -i "2143s/<bound>false/<bound>true/" run.csc
	#sed -i "2156s/<bound>false/<bound>true/" run.csc
        cp -p project-conf.h.print project-conf.h
	#sed -i "s/SINK_ADDITION 3/SINK_ADDITION 1/" project-conf.h

        cd ../br_sensor_vary
        cp -p project-conf.h.print project-conf.h
        sed -i "s/RATE 2/RATE ${RATE}/" project-conf.h
	#sed -i "s/SINK_ADDITION 3/SINK_ADDITION 1/" project-conf.h
        cd ../sender_vary
        cp -p project-conf.h.print project-conf.h
        sed -i "s/RATE 2/RATE ${RATE}/" project-conf.h
	#sed -i "s/SINK_ADDITION 3/SINK_ADDITION 1/" project-conf.h
        cd ../br_fixed
#        cp -a /home/voravit/repo/current/main/contiki/core/net/rpl.auto/* /home/voravit/repo/current/main/contiki/core/net/rpl/.
#        cp -a /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c.rank /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c
        cp -a /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c.etx.1152 /home/voravit/repo/current/main/contiki/core/net/rpl/rpl-metric-timer.c
	
	cd $BASE_COOJA/build
	FILE=${BASE_TEST}/run.csc

	echo ">>>>> Running SEED:$SEED" >> $LOGFILE
	echo "START:" `date "+%Y-%m-%d %H:%M:%S %N"` >> $LOGFILE
	echo "START:"
	#sudo ${BASE_TEST}/setup_route.sh 2 & >> $LOGFILE 2>&1
	sudo ${BASE_TEST}/setup_route.sh $SINKS $SERVER & 
	java -mx1152m -jar ${BASE_COOJA}/dist/cooja.jar -nogui=$FILE
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
	  java -mx1152m -jar ${BASE_COOJA}/dist/cooja.jar -nogui=$FILE
	  ERR=`grep "FATAL" COOJA.log | wc -l`
	  FINISHED=`grep "finished" COOJA.log | wc -l`
	done

	echo "STOP:" `date "+%Y-%m-%d %H:%M:%S %N"` >> $LOGFILE
	sleep 2
	mkdir etx_1152_${RATE}
	mv COOJA* log*txt etx_1152_${RATE}
	mv *pcap etx_1152_${RATE}
	mv tun*txt etx_1152_${RATE}
	mv nohup.out etx_1152_${RATE}
	mv route.out etx_1152_${RATE}
	mv etx_1152_${RATE} ${BASE_TEST}/.
	cd $BASE_TEST
	mv $LOGFILE etx_1152_${RATE}/.
	mv udp.out etx_1152_${RATE}/.
	mv coordinator.out etx_1152_${RATE}/.

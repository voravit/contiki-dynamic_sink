#!/bin/bash
# NUMBER OF SINKS IN THE SYSTEMS
SINKS=$1
# $2 for SERVER REPLY or NOREPLY
BASE_TEST="/home/voravit/repo/current/main/contiki/examples/ipv6/br_fixed"
BASE_COOJA="/home/voravit/repo/current/main/contiki/tools/cooja"

if [ -z "$2" ]; then
        cp -p ${BASE_TEST}/server.noreply ${BASE_TEST}/server
else
        if [ "$2" == "REPLY" ]; then
		cp -p ${BASE_TEST}/server.reply ${BASE_TEST}/server
	else
		cp -p ${BASE_TEST}/server.noreply ${BASE_TEST}/server
	fi
fi

/sbin/sysctl -w net.ipv6.conf.all.forwarding=1

IF_DUMMY=`ifconfig dummy0| grep fd00::2 | wc -l`
if [[ $IF_DUMMY -eq 0 ]];
then
	/sbin/ip link add dummy0 type dummy
        /sbin/ip link set dummy0 up
        /sbin/ip -6 addr add fd00::2/128 dev dummy0
fi

IF_LO=`ifconfig lo| grep fd00::1 | wc -l`
if [[ $IF_LO -eq 0 ]];
then
	/sbin/ifconfig lo inet6 add fd00::1/128
fi

COORDINATOR=`ps aux| grep ${BASE_TEST}/coordinator | wc -l`
if [[ $COORDINATOR -gt 1 ]];
then
	/usr/bin/killall ${BASE_TEST}/coordinator
fi

echo "START COORDINATOR PROCESS"
/usr/bin/nohup ${BASE_TEST}/coordinator > ${BASE_TEST}/coordinator.out &

UDPSERVER=`ps aux| grep ${BASE_TEST}/server| wc -l`
if [[ $UDPSERVER -gt 1 ]];
then
	/usr/bin/killall ${BASE_TEST}/server
fi

echo "START UDP SERVER PROCESS"
# START UDP SERVER PROCESS
/usr/bin/nohup ${BASE_TEST}/server > ${BASE_TEST}/udp.out &

echo "WAIT FOR COOJA"
# LOOP TO WAIT FOR THE COOJA SCRIPT TO START
FINISH=0
while [[ FINISH -eq 0 ]]
do
	# BASICALLY RUN THE LOOP EVERY SECOND
	sleep 1
	#FINISH=`grep "deactivated" ${BASE_COOJA}/build/COOJA.log | wc -l`
	FINISH=`grep "SerialSocketServer" ${BASE_COOJA}/build/COOJA.log | wc -l`
done

echo "SETUP ROUTE"
for i in `seq 0 $(($SINKS-1))`
do
	sleep 0.05
	echo "/usr/bin/nohup /bin/sh -c '${BASE_TEST}/tunslip6 -a 127.0.0.1 -p 6000$(($i+1)) -t tun${i} fd00::$((11 + $i))/128 2>&1 | tee tun${i}.txt' &"
	/usr/bin/nohup /bin/sh -c "${BASE_TEST}/tunslip6 -a 127.0.0.1 -p 6000$(($i+1)) -t tun${i} fd00::$((11 + $i))/128 2>&1 | tee tun${i}.txt" &
done

echo "ROUTE MONITOR START"
${BASE_TEST}/route_monitor.sh $SINKS > route.out 2>&1
/bin/chown voravit:voravit route.out
/bin/chmod 644 route.out
/bin/chown voravit:voravit nohup.out
/bin/chmod 644 nohup.out
echo "ROUTE MONITOR STOP"
#/bin/rm nohup.out
NOHUP=`ps aux| grep nohup| wc -l`
if [[ $NOHUP -gt 1 ]];
then
	/usr/bin/killall /usr/bin/nohup ${BASE_TEST}/server
fi
SERVER=`ps aux| grep ${BASE_TEST}/server| wc -l`
if [[ $SERVER -gt 1 ]];
then
	/usr/bin/killall ${BASE_TEST}/server
fi
/bin/chown voravit:voravit ${BASE_TEST}/udp.out

COORDINATOR=`ps aux| grep ${BASE_TEST}/coordinator | wc -l`
if [[ $COORDINATOR -gt 1 ]];
then
	/usr/bin/killall ${BASE_TEST}/coordinator
fi
/bin/chown voravit:voravit ${BASE_TEST}/coordinator.out


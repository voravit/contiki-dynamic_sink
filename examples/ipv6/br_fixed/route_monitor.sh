#!/bin/bash
# NUMBER OF SINKS IN THE SYSTEMS
SINKS=$1
FILES=(tun*txt)

sleep 1
# ADD ROUTES TO SINKS AT THE START
for i in `seq 0 $(($SINKS-1))`
do
	GOT_IP=0
	while [[ $GOT_IP -eq 0 ]];
	do
		GOT_IP=`grep "==>" tun${i}.txt | cut -d ">" -f2 | grep "fd00::200" | wc -l`
		SINKIP=`grep "==>" tun${i}.txt | cut -d ">" -f2 | grep "fd00::200" | tail -1`
		#echo "SINKIP:${SINKIP} GOT_IP:${GOT_IP}"
		if [[ $GOT_IP -eq 0 ]];
		then
			sleep 1;
		fi
	done
	echo "/sbin/route -A inet6 add ${SINKIP}/128 dev tun${i}"
	/sbin/route -A inet6 add ${SINKIP}/128 dev tun${i}
	ARRAY[i]=0
done

# TRACK ROUTES OF NON-ROOT NODES 
# WE DON'T KNOW IN ADVANCE WHICH SINK A NON-ROOT NODE CONNECTS TO

FINISH=0
while [[ FINISH -eq 0 ]];
do
	# BASICALLY RUN THE LOOP EVERY SECOND
	sleep 1

	# UPDATE ROUTES ON EACH TUNNEL
	for i in `seq 0 $(($SINKS-1))`
	do
		#echo "TUNNEL: tun${i}"
		ROUTE=`grep -n -e "ROUTE ADD" -e "ROUTE REMOVE" tun${i}.txt | cut -d ":" -f1`
		for j in ${ROUTE[@]}
		do
			if [[ $j -gt ${ARRAY[${i}]} ]];
			then
				#echo $j	
				ARRAY[${i}]=${j}
				OP=`sed -n "${j}p" tun${i}.txt | cut -d " " -f3`
				PREFIX=`sed -n "${j}p" tun${i}.txt | cut -d " " -f4`

				if [[ $OP == "ADD:" ]];
				then
					# IF ROUTE EXIST, REMOVE IT
					EXIST=`route -A inet6 | grep ${PREFIX} | wc -l`
					if [[ $EXIST -gt 0 ]];
					then
						TUN=`route -A inet6 | grep ${PREFIX} | rev | cut -d "n" -f1 | rev` 
						if [[ $TUN -ne $i ]];
						then
							echo "/sbin/route -A inet6 del ${PREFIX}/128 dev tun${TUN}"
							/sbin/route -A inet6 del ${PREFIX}/128 dev tun${TUN}
							echo "/sbin/route -A inet6 add ${PREFIX}/128 dev tun${i}"
							/sbin/route -A inet6 add ${PREFIX}/128 dev tun${i}
						#else
							#echo "route exists!"
						fi
					else
						echo "/sbin/route -A inet6 add ${PREFIX}/128 dev tun${i}"
						/sbin/route -A inet6 add ${PREFIX}/128 dev tun${i}
					fi
				elif [[ $OP == "REMOVE:" ]];
				then
					echo "/sbin/route -A inet6 del ${PREFIX}/128 dev tun${i} # WE DONOT REMOVE ROUTE"
					#/sbin/route -A inet6 del ${PREFIX}/128 dev tun${i}
				fi
				
			fi
		done
	done

	#if [ -e "${FILES[0]}" ];
	if [ -f "tun0.txt" ];
	then
		FINISH=`grep "down" tun*txt | wc -l`
	else
		FINISH=1
	fi
done

/bin/chown voravit:voravit tun*txt

#for i in `seq 0 $(($SINKS-1))`
#do
#	echo "ARRAY[$i]:${ARRAY[$i]}"
#done


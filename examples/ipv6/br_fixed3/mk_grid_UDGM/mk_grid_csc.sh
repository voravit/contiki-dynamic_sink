#!/bin/bash
COOR_SINK="coordinates_sink.dat"
COOR_NONSINK="coordinates_nonsink.dat"
INPUT="template_grid.csc"
OUTPUT="grid.csc"
MOTE_FILE="template_mote.txt"
SERIAL_FILE="template_serial.txt"
cp -p $INPUT $OUTPUT
########## DELETE OUTPUT FILE IF EXISTS  ##########
#if [ -f $OUTPUT ] ; then
#    rm $OUTPUT
#fi

CTR_SINK=`cat $COOR_SINK | wc -l`
CTR_NONSINK=`cat $COOR_NONSINK | wc -l`
########## ADD TIMELINE ##########
TIMELINE=`grep -n TIMELINE $OUTPUT | cut -d ":" -f1`

TMP="tmp.dat"
if [ -f $TMP ] ; then
    rm $TMP
fi

for i in `seq 1 $(($CTR_SINK+$CTR_NONSINK))`;
do
	echo "      <mote>$(($i-1))</mote>" >> $TMP	
done
sed -i -e "${TIMELINE}r $TMP" $OUTPUT
sed -i -e "${TIMELINE}d" $OUTPUT
rm $TMP
########## ADD MOTE ##########
MOTE=`grep -n MOTE $OUTPUT | cut -d ":" -f1`
MOTE_TMP="tmp_mote.txt"

## CREATE: SINK NODES (#SINKS = $CTR_SINK)
for i in `seq 1 $CTR_SINK`;
do
	cp -p $MOTE_FILE $MOTE_TMP
	X=`sed "$i!d" $COOR_SINK | cut -d " " -f1`
	Y=`sed "$i!d" $COOR_SINK | cut -d " " -f2`
	sed -i "5s/<x>0/<x>$X/" $MOTE_TMP
	sed -i "6s/<y>0/<y>$Y/" $MOTE_TMP
	sed -i "15s/<id>0/<id>$i/" $MOTE_TMP
	cat $MOTE_TMP >> $TMP
done

## CREATE: NON-SINK NODES (#NODES = $CTR_NONSINK)
for i in `seq 1 $CTR_NONSINK`;
do
	cp -p $MOTE_FILE $MOTE_TMP
	X=`sed "$i!d" $COOR_NONSINK | cut -d " " -f1`
	Y=`sed "$i!d" $COOR_NONSINK | cut -d " " -f2`
	sed -i "5s/<x>0/<x>$X/" $MOTE_TMP
	sed -i "6s/<y>0/<y>$Y/" $MOTE_TMP
	sed -i "15s/<id>0/<id>$(($CTR_SINK+$i))/" $MOTE_TMP
	sed -i "17s/wismote1/wismote2/" $MOTE_TMP
	cat $MOTE_TMP >> $TMP
done

sed -i -e "${MOTE}r $TMP" $OUTPUT
sed -i -e "${MOTE}d" $OUTPUT

########## ADD TIMEOUT ##########
#TIMEOUT=`grep -n TIMEOUT $OUTPUT | cut -d ":" -f1`
#	sed -i -e "${TIMEOUT}s/TIMEOUT/TIMEOUT(2250000);/" $OUTPUT
	sed -i -e "s/TIMEOUT/TIMEOUT(1000000);/" $OUTPUT

########## ADD SERIAL FOR SINKS  ##########
SERIAL=`grep -n SERIAL $OUTPUT | cut -d ":" -f1`
SERIAL_TMP="tmp_serial.txt"

rm $TMP
## CREATE: SERIAL FOR SINK NODES (#SINKS = $CTR_SINK)
for i in `seq 1 $CTR_SINK`;
do
	cp -p $SERIAL_FILE $SERIAL_TMP
#	sed -i "3s/<mote_arg>0/<mote_arg>$(($i-1))/" $SERIAL_TMP
#	sed -i "9s/<z>0/<z>$(($i-1))/" $SERIAL_TMP
#	sed -i "11s/<location_x>5/<location_x>$((5+$i))/" $SERIAL_TMP
#	sed -i "12s/<location_y>400/<location_y>$((400+$i))/" $SERIAL_TMP
#	sed -i "16s/<mote_arg>0/<mote_arg>$(($i-1))/" $SERIAL_TMP
#	sed -i "18s/<port>60001/<port>$(($i + 60000))/" $SERIAL_TMP
#	sed -i "22s/<z>0/<z>$(($i-1))/" $SERIAL_TMP
#	sed -i "24s/<location_x>500/<location_x>$((500+$i))/" $SERIAL_TMP
#	sed -i "25s/<location_y>500/<location_y>$((500+$i))/" $SERIAL_TMP
	sed -i "3s/<mote_arg>0/<mote_arg>$(($i-1))/" $SERIAL_TMP
	sed -i "5s/<port>60001/<port>$(($i + 60000))/" $SERIAL_TMP
	sed -i "9s/<z>0/<z>$(($i-1))/" $SERIAL_TMP
	sed -i "11s/<location_x>500/<location_x>$((500+$i))/" $SERIAL_TMP
	sed -i "12s/<location_y>500/<location_y>$((500+$i))/" $SERIAL_TMP
	cat $SERIAL_TMP >> $TMP
done

sed -i -e "${SERIAL}r $TMP" $OUTPUT
sed -i -e "${SERIAL}d" $OUTPUT

rm tmp*txt

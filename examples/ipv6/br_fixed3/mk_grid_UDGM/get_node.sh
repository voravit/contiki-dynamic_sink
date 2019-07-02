#!/bin/bash
if [ -f coordinates_nonsink.dat ] ; then
    rm coordinates_nonsink.dat
fi

# CBR 
sed -n "1,10p" r1 >> coordinates_nonsink.dat
sed -n "1,10p" r2 >> coordinates_nonsink.dat
sed -n "1,10p" r3 >> coordinates_nonsink.dat
sed -n "1,10p" r4 >> coordinates_nonsink.dat

# POISSON
sed -n "11,20p" r1 >> coordinates_nonsink.dat
sed -n "11,20p" r2 >> coordinates_nonsink.dat
sed -n "11,20p" r3 >> coordinates_nonsink.dat
sed -n "11,20p" r4 >> coordinates_nonsink.dat

# VARY
sed -n "21,25p" r1 >> coordinates_nonsink.dat
sed -n "21,25p" r2 >> coordinates_nonsink.dat
sed -n "21,25p" r3 >> coordinates_nonsink.dat
sed -n "21,25p" r4 >> coordinates_nonsink.dat

if [ -f coordinates_sink.dat ] ; then
    rm coordinates_sink.dat
fi

# CBR 
echo "150 150" >> coordinates_sink.dat
sed -n "98,100p" r1 >> coordinates_sink.dat
sed -n "98,100p" r2 >> coordinates_sink.dat
sed -n "98,100p" r3 >> coordinates_sink.dat
sed -n "98,100p" r4 >> coordinates_sink.dat


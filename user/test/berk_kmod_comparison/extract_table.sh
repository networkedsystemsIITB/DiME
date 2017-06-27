#!/bin/bash

for f in `ls *redis*run*log`; 
do
	#echo -n $f | tr '_.' ' ' | awk '{print $2 "\t" $3 "\t" $7 "\t" $9 "\t" $11 "\t" $12;}' | tr "\n" '\t';
	echo -n $f | tr '-' ' ';
	cat $f | grep "Throughput(ops/sec)" | awk -v FS='[ ,]+' '{print " ", $3;}' | tr "\n" '\t';
	pagefaults=`cat $f | grep "pagefault" | awk -v FS='[ ,]+' '{print " ", $3;}'`
	echo "$pagefaults";
	
done;

# to take average of all tests
#../extract_table.sh | awk -F"\t" '{array[$2"\t"$3"\t"$4"\t"$5]+=$6; count[$2"\t"$3"\t"$4"\t"$5]+=1;} END { for (i in array) {print i"\t" array[i]/count[i]}}'

#!/bin/bash
declare -A pairs
for f in `ls *instance-1*run*log`; 
do
	#echo -n $f | tr '_.' ' ' | awk '{print $2 "\t" $3 "\t" $7 "\t" $9 "\t" $11 "\t" $12;}' | tr "\n" '\t';
	echo -n $f | tr '-' ' ';
	echo -n " throughput " ; 
	cat $f | grep "Throughput(ops/sec)" | awk -v FS='[ ,]+' '{print " ", $3;}' | tr "\n" '\t';

	cat $f | grep '^inst: 0' | while read -r line;
	do
		instid=$(echo $line | sed 's/[ \t\r\n:]\+/ /g' | cut -d' ' -f2);
		perm=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f3);
		pfcount=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f4);

		pairs["${instid}-${perm}"]=${pfcount};
		echo -n " RUN:${instid}-${perm} ${pfcount}";
	done;
	fload=$(echo $f | sed 's/-run/-load/g')
	cat $fload | grep '^inst: 0' | while read -r line;
	do
		instid=$(echo $line | sed 's/[ \t\r\n:]\+/ /g' | cut -d' ' -f2);
		perm=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f3);
		pfcount=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f4);

		echo -n " LOAD:${instid}-${perm} ${pfcount}";
		#echo -n " ${instid}-${perm} ${pairs[${instid}-${perm}]}";
	done;

	if [ $(echo $f | grep "kmod-test_mode-separate" | wc -l) -gt 0 ];
	then
		cat $f | grep '^inst: 1' | while read -r line;
		do
			instid=$(echo $line | sed 's/[ \t\r\n:]\+/ /g' | cut -d' ' -f2);
			perm=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f3);
			pfcount=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f4);

			pairs["${instid}-${perm}"]=${pfcount};
			echo -n " RUN:${instid}-${perm} ${pfcount}";
		done;
		fload=$(echo $f | sed 's/-run/-load/g')
		cat $fload | grep '^inst: 1' | while read -r line;
		do
			instid=$(echo $line | sed 's/[ \t\r\n:]\+/ /g' | cut -d' ' -f2);
			perm=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f3);
			pfcount=$(echo $line | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f4);

			echo -n " LOAD:${instid}-${perm} ${pfcount}";
			#echo -n " ${instid}-${perm} ${pairs[${instid}-${perm}]}";
		done;
	fi
	echo ""
done;

# to take average of all tests
#../extract_table.sh | awk -F"\t" '{array[$2"\t"$3"\t"$4"\t"$5]+=$6; count[$2"\t"$3"\t"$4"\t"$5]+=1;} END { for (i in array) {print i"\t" array[i]/count[i]}}'

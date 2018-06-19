#!/bin/awk

BEGIN {
}

{
	if(FNR == 1) {
		for(i = 1; i <= NF; i++) {
			labels[i] = $i;
		}
	} else if(FNR == 2) {
		for(i = 1; i <= NF; i++) {
			values[i] = $i;
		}
	}
}

END {
	for(i in labels) {
		printf " %s %s", labels[i], values[i];
	}
}

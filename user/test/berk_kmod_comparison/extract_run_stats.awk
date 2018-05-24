#!/bin/awk

BEGIN {
}

{
	if(NR == 1) {
		for(i = 1; i <= NF; i++) {
			labels[i] = $i;
		}
	} else if(FNR == 2) {
		if(FNR==NR) {
			for(i = 1; i <= NF; i++) {
				values[i] = $i;
			}
		} else {
			for(i = 1; i <= NF; i++) {
				if(labels[i] != "instance_id" &&
					labels[i] != "latency_ns" &&
					labels[i] != "bandwidth_bps" &&
					labels[i] != "local_npages" &&
					labels[i] != "cpu_per_pf" &&
					labels[i] != "pid") {
					values[i] -= $i;
				}
			}
		}
	}
}

END {
	for(i in labels) {
		printf " %s %s", labels[i], values[i];
	}
}

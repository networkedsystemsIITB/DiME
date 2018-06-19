#!/bin/awk

BEGIN {
	
}

{
	if(NR == FNR) {
		split($1,range,"-")
		maps[NR] = $0
		range_start[NR] = range[1]
		range_end[NR] = range[2]
	} else {
		found=0
		if($4 == "add_page:") {
			for (i in range_start) {
				if(range_start[i] <= $9 && $9 < range_end[i]) {
					count[i]++;
					found++
				}
			}
			if(found==0 || found>1) {
				print "cannot found vma", found, $0
			}
		}
	}
}

END {
	FS="\t"
	for (i in maps) {
		if(count[i] == "") {
			count[i] = 0
		}
		print count[i], range_start[i], range_end[i], maps[i]
	}
}

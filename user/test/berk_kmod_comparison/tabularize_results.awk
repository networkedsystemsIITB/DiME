#!/bin/awk

BEGIN {
	last_col_index = 1
}

{
	for (i = 1 ; i <= NF ; i += 2) {
		if(length(dict_labels[$i]) == 0) {
			dict_labels[$i] = last_col_index
			dict_order[last_col_index] = $i
			last_col_index++
		}
		dict[$i][NR] = $(i+1)
	}
}

END {
	for(i in dict_order) {
		printf("%s\t", dict_order[i]);
	}
	for(i in dict["test"]) {
		print " ";
		for(j in dict_order) {
			printf("%s\t", dict[dict_order[j]][i]);
		}
	}
	print "";
}

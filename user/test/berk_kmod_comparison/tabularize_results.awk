#!/bin/awk

BEGIN {
}

{
	delete vals;
	for (i = 1 ; i <= NF ; i += 2) {
		if (length(label[i]) == 0)
		{
			label[i] = $i;
		} else if (label[i] != $i) {
			print "LABEL MISMATCH\n\n";
			next
		}
		vals[int((i+1)/2)];
		printf("%s\t", $(i+1));
	}
	print ""
}

END {
	print " ";
	for(i in label) {
		printf("%s\t", label[i]);
	}
	print "";
}

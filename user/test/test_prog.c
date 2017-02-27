#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

char *pages = NULL;
unsigned long npages = 0;

int main ( int argc, char *argv[] )
{
	int i=0;
	clock_t timer;

	if(argc < 2) {
		printf("number of pages required\n");
		exit(1);
	}

	npages = atoi(argv[1]);
	pages = (char*)malloc(getpagesize()*npages);
	if(!pages) {
		printf("error in allocating pages\n");
		exit(2);
	}

	printf("PID : %d\n", getpid());
	printf("Press ENTER to continue..");
	getchar();
	printf("Accessing pages\n");

	timer = clock();
	for(i=0 ; i<npages ; i++) {
		pages[getpagesize()*i]++;
	}

	timer = clock() - timer;

	printf("Time taken : %lu\n", timer);
	return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <unistd.h>
#include "tsc/tsc.h"

char *pages = NULL;
unsigned long long npages = 0, fault_count;
uint64_t start, end, overhead;

/**
 *	Test functions
 *
 */
void access_all_data() {
	unsigned long long i=0;

	for(i=0 ; i<npages ; i++) {
		char *pointer = &pages[i*getpagesize()+1000];
		//start = bench_start();
		//usleep(50000);
		*pointer = 100;
		//end = bench_end();
		//printf("Total time taken :, %10lu, ns, %10lx, address\n", (end - start - overhead)*1000000/3092973, ((unsigned long)(pointer) & (~((unsigned long)0xFFF))));
	}
}

void sequential_access_test() {
	//clock_t timer;
	//unsigned long long t;
	// sequential access
	//fault_count = 0;
	//printf("Sequential : \n\tTime taken (ms) \t: ");
	//timer = clock();

	overhead = measure_tsc_overhead();
	access_all_data();

	//timer = clock() - timer;
	//printf("%lu\n", timer);
	//fault_count = get_page_fault_count() - fault_count;
	//printf("\tTotal page faults \t: %llu\n", fault_count);
	//t = timer;
	//fault_count = t/(fault_count==0?1:fault_count);
	//printf("\tTime per pagefault(ms) \t: %llu\n\n", fault_count);
}

void start_test() {
	unsigned long long i=0, r=0;
	clock_t timer;

	printf("Assumming module inserted and setup.\n");

	sequential_access_test();
}

void sig_handler(int signo)
{
    if (signo == SIGUSR1)
    	start_test();

    exit(0);
}


int main ( int argc, char *argv[] )
{
	unsigned long long i=0,j=0;
	clock_t timer;

	if(argc < 2) {
		printf("number of pages required %d\n", getpagesize());
		exit(1);
	}

	printf("PID : %d\n", getpid());
	
	sscanf(argv[1], "%llu", &npages);
	printf("Allocating %llu pages\n", npages);
	pages = (char*)malloc(getpagesize()*npages);
	if(!pages) {
		printf("error in allocating pages\n");
		exit(2);
	}

	for(i=0 ; i<npages ; i++) {
		for(j=0 ; j<getpagesize() ; ++j)
			pages[i*j]=200;
	}

	// Access all once before starting
	//printf("First dry run..\n\n");
	//access_all_data();


	// Register user signal
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        printf("\ncan't catch SIGUSR1\n");
        return 1;
    }

	// Wait for module to insert and signal from user space
	printf("Send signal to start tests..\n");

    // A long long wait so that we can easily issue a signal to this process
    while(1) 
        sleep(10000);
	
	return 0;
}

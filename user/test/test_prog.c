#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

char *pages = NULL;
unsigned long long npages = 0;

void start_test();

void sig_handler(int signo)
{
    if (signo == SIGUSR1)
    	start_test();

    exit(0);
}


/**
 *	Test functions
 *
 */
void access_all_data() {
	unsigned long long i=0;
	for(i=0 ; i<npages ; i++) {
		pages[i*getpagesize()]++;
	}
}
void sequential_access_test() {
	clock_t timer;

	// sequential access
	printf("Sequential : \n\tTime taken (ms) \t: ");
	timer = clock();
	access_all_data();
	timer = clock() - timer;
	printf("%lu\n", timer);
	printf("\tPer page time (ms) \t: %llu\n\n", timer/npages);
}


int main ( int argc, char *argv[] )
{
	unsigned long long i=0;
	clock_t timer;

	if(argc < 2) {
		printf("number of pages required\n");
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
/*
	// Access all once before starting
	printf("First dry run..\n\n");
	access_all_data();

	// Sequence access
	printf("Run tests before module insertion : \n");
	sequential_access_test();
*/


	// Wait for module to insert and signal from user space
	printf("Send signal to start tests..\n");


	// Register user signal
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        printf("\ncan't catch SIGUSR1\n");
        return 1;
    }

    // A long long wait so that we can easily issue a signal to this process
    while(1) 
        sleep(10000);
	
	return 0;
}

void start_test() {
	unsigned long long i=0, r=0;
	clock_t timer;

	printf("Assumming module inserted and setup.\n");

	sequential_access_test();

/*
	// only one page
	timer = clock();
	r = 20;
	for(i=0 ; i<npages ; i++) {
		pages[getpagesize()*r]++;
	}
	timer = clock() - timer;
	printf("Single page : Time taken : %lu\n", timer);


	// two pages
	timer = clock();
	r = 200;
	for(i=0 ; i<npages ; i+=r) {
		int j;
		for (j=0 ; j<r ; ++j) {
			pages[getpagesize()*(r)]++;
		}
	}
	timer = clock() - timer;
	printf("Two pages alternate : Time taken : %lu\n", timer);*/
}
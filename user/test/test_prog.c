#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

char *pages = NULL;
unsigned long long npages = 0, fault_count, testcase;

unsigned long long get_page_fault_count() {
	FILE *f = fopen("/sys/module/kmodule/parameters/page_fault_count", "r");
	unsigned long long count;

	fscanf(f, "%llu", &count);

	fclose(f);

	return count;
}

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
		pages[i*getpagesize()]+=5;
	}
}
void sequential_access_test() {
	clock_t timer;
	unsigned long long t;
	// sequential access
	fault_count = 0;
	printf("[TEST]:	Single sequential access test: \n[TEST]:	\tTime taken (ms) \t: ");
	timer = clock();

	access_all_data();

	timer = clock() - timer;
	printf("%lu\n", timer);
}

void double_sequential_access_test() {
	clock_t timer;
	unsigned long long t;
	// sequential access
	fault_count = 0;
	printf("[TEST]:	Double sequential access test: \n[TEST]:	\tTime taken (ms) \t: ");
	timer = clock();

	access_all_data();
	access_all_data();

	timer = clock() - timer;
	printf("%lu\n", timer);
}


int main ( int argc, char *argv[] ) {
	unsigned long long i=0;
	clock_t timer;

	if(argc < 2) {
		printf("[TEST]:	number of pages required\n");
		exit(1);
	} else if(argc < 3) {
		printf("[TEST]:	testcase number required\n");
		exit(1);
	} 

	printf("[TEST]:	PID of this process : %d\n", getpid());
	
	sscanf(argv[1], "%llu", &npages);
	sscanf(argv[2], "%llu", &testcase);
	printf("[TEST]:	Allocating %llu pages\n", npages);
	pages = (char*)malloc(getpagesize()*npages);
	if(!pages) {
		printf("[TEST]:	error in allocating pages\n");
		exit(2);
	}

	// Wait for module to insert and signal from user space
	printf("[TEST]:	Send signal to start tests..\n");


	// Register user signal
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        printf("\n[TEST]:	can't catch SIGUSR1\n");
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

	printf("[TEST]:	Assumming module inserted and setup.\n");
	printf("[TEST]:	Starting testcase %llu.\n", testcase);

	switch(testcase) {
		case 1:
			sequential_access_test();
			break;
		case 2:
			double_sequential_access_test();
			break;
		default:
			printf("[TEST]:	ERROR: Unable to run testcase : %llu.\n", testcase);
			break;
	}
}

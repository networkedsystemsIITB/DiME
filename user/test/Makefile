all:
	gcc -O0 test_prog.c -o test_prog -g
	cd microbench && $(MAKE)

clean:
	rm test_prog
	cd microbench && $(MAKE) clean

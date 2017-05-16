all:
	cd kernel && $(MAKE)
	cd user/test && $(MAKE) 

clean:
	cd kernel && $(MAKE) clean
	cd user/test && $(MAKE) clean

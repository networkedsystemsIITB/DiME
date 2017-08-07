# **Di**saggregated **M**emory **E**mulator (**D**i**ME**)

Resource disaggregation is a new design paradigm for datacenter servers, where the compute, memory, storage, and I/O resources of servers are disaggregated and connected by a high-speed interconnect network. Resource disaggregation, and memory disaggregation in particular, can have significant impact on application performance due to the increased latency in accessing a portion of the system’s memory remotely. While applications need to be redesigned and optimized to work well on these new architectures, the unavailability of commodity disaggregated memory hardware makes it difficult to evaluate any such optimizations. To address this issue, our work develops an emulator Dime, for disaggregated memory systems. Our tool can emulate different access latencies over different parts of an application’s memory image as specified by the user. We evaluate our tool extensively using popular datacenter workloads to demonstrate its efficacy and usefulness, and show that it outperforms previous emulators in its ability to emulate different access delays at a fine per-page granularity.

## User's Guide
### Installation
Clone github repository:
```sh
$ git clone <github repo url>
$ git submodule update --init --recursive
$ cd DiME && make
```
##### Add hooks to `do_page_fault` function:
1) Edit `<linux source>/arch/<arch>/mm/fault.c` file to add the following hook function pointer declarations.
    ```c
    int (*do_page_fault_hook_start)(
        				struct pt_regs *regs,
        				unsigned long error_code,
        				unsigned long address,
        				int * hook_flag,
        				ulong * hook_timestamp) = NULL;
    EXPORT_SYMBOL(do_page_fault_hook_start);
    int (*do_page_fault_hook_end)(
        				struct pt_regs *regs,
        				unsigned long error_code,
        				unsigned long address,
        				int * hook_flag,
        				ulong * hook_timestamp) = NULL;
    EXPORT_SYMBOL(do_page_fault_hook_end);
    ```

2) Call the start and end hook functions inside `do_page_fault` function as follows.
    ```c
    do_page_fault(struct pt_regs *regs, unsigned long error_code)
    {
        .......
    	// Calling do_page_fault_hook start hook function
    	int hook_flag		    = 0;
    	ulong hook_timestamp    = 0;
    	if(do_page_fault_hook_start != NULL) {
    		do_page_fault_hook_start(regs, error_code, address, &hook_flag, &hook_timestamp);
    	}
    	
    	prev_state = exception_enter();
    	__do_page_fault(regs, error_code, address);
    	exception_exit(prev_state);
    
    	// Calling do_page_fault_hook end hook function
    	if(do_page_fault_hook_end != NULL) {
    		do_page_fault_hook_end(regs, error_code, address, &hook_flag, &hook_timestamp);
    	}
    }
    ```
3) Now compile and update the kernel.
    ```sh
    $ make && make install
    ```

### Usage
Use `./user/tools/insert_module.sh` script to insert DiME module with a list of PIDs and a config file.
```
$ ./user/tools/insert_module.sh -h
Usage : ./user/tools/insert_module.sh [OPERATION]..
Insert module with config file

Mandatory arguments to long options are mandatory for short options too.
-p, --pids       <value>   a comma (,) separated list of pids to add into emulator
-c, --config     <value>   path to config file
```
Refer to `./user/tools/config_template.conf` config template file for DiME configuration parameters.

## Developer's Guide
A basic FIFO page eviction policy is available currently. DiME is modularized so that other developers can develope and add a custome page eviction policy as a separate module. To develope a new eviction policy module, developer is required to implement various operations specified in `page_replacement_policy_struct` structure defined in `kernel/common.h`. 
```c
struct page_replacement_policy_struct {
	int    (*add_page)  (struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address);
	void   (*clean)     (struct dime_instance_struct *dime_instance);
};
```
To register/unregister the policy with main DiME module, use `(de)register_page_replacement_policy` function with a pointer to `page_replacement_policy_struct`.

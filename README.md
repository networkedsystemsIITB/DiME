# **Di**saggregated **M**emory **E**mulator (**D**i**ME**)

Resource disaggregation is a new design paradigm for datacenter servers, where the compute, memory, storage, and I/O resources of servers are disaggregated and connected by a high-speed interconnect network. Resource disaggregation, and memory disaggregation in particular, can have significant impact on application performance due to the increased latency in accessing a portion of the system’s memory remotely. While applications need to be redesigned and optimized to work well on these new architectures, the unavailability of commodity disaggregated memory hardware makes it difficult to evaluate any such optimizations. DiME is developed to address these issues. DiME can emulate different access latencies over different parts of an application’s memory image as specified by the user. 


## User's Guide
DiME is compatible with Linux Kernel 4.9.44 (longterm).
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
        // Calling do_page_fault_hook_start hook function
        int hook_flag          = 0;
        ulong hook_timestamp   = 0;
        if(do_page_fault_hook_start != NULL) {
            do_page_fault_hook_start(regs, error_code, address, &hook_flag, &hook_timestamp);
        }
        
        prev_state = exception_enter();
        __do_page_fault(regs, error_code, address);
        exception_exit(prev_state);
    
        // Calling do_page_fault_hook_end hook function
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

Multiple instances of DiME can be configured using procfs file, `/proc/dime_config`. Reading the proc file lists the instances configured with configuration parameters. These parameters can be modified by writing configuration string to the proc file as follows:
```sh
$ make
$ insmod kernel/kmodule.ko
$ echo "instance_id=0 latency_ns=1000 bandwidth_bps=1000000000 local_npages=2000 pid=1234,5678" > /proc/dime_config
$ cat /proc/dime_config
instance_id latency_ns bandwidth_bps local_npages page_fault_count pid
          0       1000    1000000000         2000                0 1234,5678,
```
`instance_id` parameter is mandatory for any change in configuration. A new instance can be added by setting `instance_id` to +1 of max available instance; in above case it is 1.
e.g.:
```sh
$ echo "instance_id=1 latency_ns=500 bandwidth_bps=1000000000 local_npages=5000 pid=8765,4321" > /proc/dime_config
$ cat /proc/dime_config
instance_id latency_ns bandwidth_bps local_npages page_fault_count pid
          0       1000    1000000000         2000                0 1234,5678,
          1        500    1000000000         5000                0 8765,4321,
```
Note: changes in pid list must be followed by insertion of page replacement policy module, if already inserted, remove and re-insert the policy module.

Note: check `dmesg` for any errors while modifying the configuration.

## Developer's Guide
A basic FIFO page eviction policy is available currently. DiME is modularized so that other developers can develope and add a custome page eviction policy as a separate module. To develope a new eviction policy module, developer is required to implement various operations specified in `page_replacement_policy_struct` structure defined in `kernel/common.h`. 
```c
struct page_replacement_policy_struct {
    int    (*add_page)  (struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address);
    void   (*clean)     (struct dime_instance_struct *dime_instance);
};
```
To register/unregister the policy with main DiME module, use `(de)register_page_replacement_policy` function with a pointer to `page_replacement_policy_struct`.

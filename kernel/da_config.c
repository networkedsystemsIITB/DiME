#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "da_config.h"

#define PROCFS_MAX_SIZE     102400
#define PROCFS_NAME         "dime_config"
static char procfs_buffer[PROCFS_MAX_SIZE];     // The buffer used to store character for this module
static unsigned long procfs_buffer_size = 0;    // The size of the buffer

static ssize_t procfile_read(struct file*, char*, size_t, loff_t*);
static ssize_t procfile_write(struct file *, const char *, size_t, loff_t *);

struct proc_dir_entry *dime_config_entry;

static struct file_operations cmd_file_ops = {  
    .owner = THIS_MODULE,
    .read = procfile_read,
    .write = procfile_write,
};

int init_dime_config_procfs(void) {
    dime_config_entry = proc_create(PROCFS_NAME, S_IFREG | S_IRUGO, NULL, &cmd_file_ops);

    if (dime_config_entry == NULL) {
        remove_proc_entry(PROCFS_NAME, NULL);

        DA_ALERT("Could not initialize /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;
    }

    /*
     * KUIDT_INIT is a macro defined in the file 'linux/uidgid.h'. KGIDT_INIT also appears here.
     */
    proc_set_user(dime_config_entry, KUIDT_INIT(0), KGIDT_INIT(0));
    proc_set_size(dime_config_entry, 37);

    DA_INFO("proc entry \"/proc/%s\" created\n", PROCFS_NAME);
    return 0;
}

void cleanup_dime_config_procfs(void) {
    remove_proc_entry(PROCFS_NAME, NULL);
    DA_INFO("proc entry \"/proc/%s\" removed\n", PROCFS_NAME);
}

static ssize_t procfile_read(struct file *file, char *buffer, size_t length, loff_t *offset) {
    int ret;
    int seg_size;
    
    if(*offset == 0) {
        // offset is 0, so first call to read the file.
        // Initialize buffer with config parameters currently set
        int i, j;
        procfs_buffer_size = sprintf(procfs_buffer, "Instance_ID latency_ns bandwidth_bps local_npages page_fault_count PIDs\n");
        for(i=0 ; i<dime.dime_instances_size ; ++i) {
            procfs_buffer_size += sprintf(procfs_buffer+procfs_buffer_size, 
                                            "%11d %10lu %13lu %12lu %16lu ", dime.dime_instances[i].instance_id,
                                                                        dime.dime_instances[i].latency_ns,
                                                                        dime.dime_instances[i].bandwidth_bps,
                                                                        dime.dime_instances[i].local_npages,
                                                                        dime.dime_instances[i].page_fault_count);
            for(j=0 ; j<dime.dime_instances[i].pid_count ; ++j) {
                procfs_buffer_size += sprintf(procfs_buffer+procfs_buffer_size, "%d,", dime.dime_instances[i].pid[j]);
            }
            procfs_buffer[procfs_buffer_size++] = '\n';
        }
    }

    // calculate max size of block that can be read
    seg_size = length < procfs_buffer_size ? length : procfs_buffer_size;
    if (*offset >= procfs_buffer_size) {
        ret  = 0;   // offset value beyond the available data to read, finish reading
    } else {
        memcpy(buffer, procfs_buffer, seg_size);
        *offset += seg_size;    // increment offset value
        ret = seg_size;         // return number of bytes read
    }

    return ret;
}

long long int update_instance_id = -1;
int update_pids[1000]; 
long long int update_pid_count = -1;
long long int update_pids_append_flag = 0;
long long int update_latency_ns = -1;
long long int update_bandwidth_bps = -1;
long long int update_local_npages = -1;
long long int update_page_fault_count = -1;


void set_config_param(char *key, char *value) {
    int err;
    long long_val;
    // first trim the strings
    key = strim(key);
    value = strim(value);

    if(strcmp(key, "instance_id") == 0) {
        DA_ERROR("Setting instance_id : %s", value);
        err = kstrtol(value, 10, &long_val);
        if(err != 0) {
            DA_ERROR("invalid number : %s (error:%d)", value, err);
            return;
        } else {
            update_instance_id = long_val;
        }
    } else if(strcmp(key, "pids") == 0) {
        char *pid_start, *pid_end;
        pid_end = pid_start = value;
        while( (pid_start = strsep(&pid_end, ",")) != NULL) {
            if(strlen(pid_start) == 0)
                continue;       // invalid token, try again

            DA_ERROR("Adding PID : %s", pid_start);
            // TODO:: append mode for '+'
            err = kstrtol(pid_start, 10, &long_val);
            if(err != 0) {
                DA_ERROR("invalid number : %s (error:%d)", pid_start, err);
                return;
            } else {
                if(update_pid_count == -1)
                    update_pid_count = 0;
                update_pids[update_pid_count++] = long_val;
            }
        }
    } else if(strcmp(key, "latency_ns") == 0) {
        DA_ERROR("Setting latency_ns : %s", value);
        err = kstrtol(value, 10, &long_val);
        if(err != 0) {
            DA_ERROR("invalid number : %s (error:%d)", value, err);
            return;
        } else {
            update_latency_ns = long_val;
        }
    } else if(strcmp(key, "bandwidth_bps") == 0) {
        DA_ERROR("Setting bandwidth_bps : %s", value);
        err = kstrtol(value, 10, &long_val);
        if(err != 0) {
            DA_ERROR("invalid number : %s (error:%d)", value, err);
            return;
        } else {
            update_bandwidth_bps = long_val;
        }
    } else if(strcmp(key, "local_npages") == 0) {
        DA_ERROR("Setting local_npages : %s", value);
        err = kstrtol(value, 10, &long_val);
        if(err != 0) {
            DA_ERROR("invalid number : %s (error:%d)", value, err);
            return;
        } else {
            update_local_npages = long_val;
        }
    } else if(strcmp(key, "page_fault_count") == 0) {
        DA_ERROR("Setting page_fault_count : %s", value);
        err = kstrtol(value, 10, &long_val);
        if(err != 0) {
            DA_ERROR("invalid number : %s (error:%d)", value, err);
            return;
        } else {
            update_page_fault_count = long_val;
        }
    } else {
        DA_ERROR("invalid config parameter : %s", key);
        return;
    }
}

static ssize_t procfile_write(struct file *file, const char *buffer, size_t length, loff_t *offset) {
    char *token_start, *token_end;

    //TODO:: handle multiple calls in segments of string to write
    procfs_buffer_size = length;
    if (procfs_buffer_size > PROCFS_MAX_SIZE) {
        procfs_buffer_size = PROCFS_MAX_SIZE;   // if length is greater than available buffer size, then read max data
        DA_WARNING("procfile buffer overflow");
    }

    if ( copy_from_user(procfs_buffer, buffer, procfs_buffer_size) ) {
        return -EFAULT;
    }

    // reset update parameters
    update_instance_id = -1;
    update_pid_count = -1;
    update_pids_append_flag = 0;
    update_latency_ns = -1;
    update_bandwidth_bps = -1;
    update_local_npages = -1;
    update_page_fault_count = -1;


    *offset += procfs_buffer_size;
    procfs_buffer[procfs_buffer_size] = '\0';

    token_start = token_end = procfs_buffer;
    while( (token_start = strsep(&token_end, " ")) != NULL) {
        char *key, *value, keyvalpair[1000];
        if(strlen(token_start) == 0)
            continue;

        strcpy(keyvalpair, token_start);
        key = value = keyvalpair;
        key = strsep(&value, "=");

        if(key && value)
            set_config_param(key, value);
    }

    /*
     * Now, set the modified config parameters
     */

    // Check if instance_id is given
    if(update_instance_id == -1) {
        DA_ERROR("missing mandatory instance_id parameter");
        return -EINVAL;
    } else if (update_instance_id > dime.dime_instances_size) {
        DA_ERROR("instance id of new instance must be +1 of max instance_id");
        return -EINVAL;
    } else {
        if(dime.dime_instances_size-1 < update_instance_id) {
            dime.dime_instances_size = update_instance_id+1;
            dime.dime_instances[update_instance_id].instance_id         = update_instance_id;
            dime.dime_instances[update_instance_id].latency_ns          = 10000ULL;
            dime.dime_instances[update_instance_id].bandwidth_bps       = 10000000000ULL;
            dime.dime_instances[update_instance_id].local_npages        = 20ULL;
            dime.dime_instances[update_instance_id].page_fault_count    = 0ULL;
            dime.dime_instances[update_instance_id].pid_count           = 0;
        }
    }

    if(update_pid_count != -1) {
        int i=0;
        for(i=0 ; i<update_pid_count ; i++) {
            dime.dime_instances[update_instance_id].pid[dime.dime_instances[update_instance_id].pid_count++] = update_pids[i];
            // TODO:: protect this pid here, or later in policy
            // TODO:: assumming instance_id and array index are same
            // TODO:: check if pid exists before adding to list
        }
    }

    if(update_local_npages != -1) {
        dime.dime_instances[update_instance_id].local_npages = update_local_npages;
    }

    if(update_latency_ns != -1) {
        dime.dime_instances[update_instance_id].latency_ns = update_latency_ns;
    }

    if(update_bandwidth_bps != -1) {
        dime.dime_instances[update_instance_id].bandwidth_bps = update_bandwidth_bps;
    }

    if(update_page_fault_count != -1) {
        dime.dime_instances[update_instance_id].page_fault_count = update_page_fault_count;
    }

    return procfs_buffer_size;
}
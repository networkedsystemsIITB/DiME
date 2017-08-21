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

        printk(KERN_ALERT "Error: Could not initialize /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;
    }

    /*
     * KUIDT_INIT is a macro defined in the file 'linux/uidgid.h'. KGIDT_INIT also appears here.
     */
    proc_set_user(dime_config_entry, KUIDT_INIT(0), KGIDT_INIT(0));
    proc_set_size(dime_config_entry, 37);

    printk(KERN_INFO "/proc/%s created\n", PROCFS_NAME);
    return 0;
}

void cleanup_dime_config_procfs(void) {
    remove_proc_entry(PROCFS_NAME, NULL);
    printk(KERN_INFO "/proc/%s removed\n", PROCFS_NAME);
}

static ssize_t procfile_read(struct file *file, char *buffer, size_t length, loff_t *offset) {
    int ret;
    int seg_size;
    
    //printk(KERN_INFO "procfile_read (/proc/%s) called; string: %s; offset:%llu\n", PROCFS_NAME, procfs_buffer, *offset);
    
    if(*offset == 0) {
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

    seg_size = length < procfs_buffer_size ? length : procfs_buffer_size;
    if (*offset >= procfs_buffer_size) {
        /* we have finished to read, return 0 */
        ret  = 0;
    } else {
        /* fill the buffer, return the buffer size */
        memcpy(buffer, procfs_buffer, seg_size);
        *offset = seg_size;
        ret = seg_size;
    }

    return ret;
}

static ssize_t procfile_write(struct file *file, const char *buffer, size_t length, loff_t *offset) {
    /* get buffer size */

    procfs_buffer_size = length;
    if (procfs_buffer_size > PROCFS_MAX_SIZE) {
        procfs_buffer_size = PROCFS_MAX_SIZE;
    }

    /* write data to the buffer */
    if ( copy_from_user(procfs_buffer, buffer, procfs_buffer_size) ) {
        return -EFAULT;
    }
    
    printk(KERN_INFO "procfile_write (/proc/%s) called; length:%lu; string: %s\n", PROCFS_NAME, length, procfs_buffer);

    *offset = procfs_buffer_size;
    return procfs_buffer_size;
}
#ifndef __DA_DEBUG_H__
#define __DA_DEBUG_H__


#ifdef __KERNEL__

#include <linux/kernel.h>    // included for KERN_INFO

extern unsigned int da_debug_flag;


#define LOG_INFO    KERN_INFO
#define LOG_WARNING KERN_WARNING
#define LOG_ERR     KERN_ERR
#define LOG_ALERT   KERN_ALERT

#define PRINT_LOG(LOG_FLAG, format, args...) printk(LOG_FLAG "[DA]:" format "\n", ## args)


#else//__KERNEL__

#include <syslog.h>

extern unsigned int da_debug_flag;

#endif//__KERNEL__




#define DA_DEBUG_ALERT_FLAG     0x00000001
#define DA_DEBUG_WARNING_FLAG   0x00000002
#define DA_DEBUG_ERROR_FLAG     0x00000004
#define DA_DEBUG_INFO_FLAG      0x00000008
#define DA_DEBUG_ENTRYEXIT_FLAG 0x00000010

#define DA_ENTRY()                                                                      \
do {                                                                                    \
    if (da_debug_flag & DA_DEBUG_ENTRYEXIT_FLAG) {                                      \
        PRINT_LOG(LOG_INFO,"[ENTRY] :%s:%d\t: ", __FUNCTION__,__LINE__);                \
    }                                                                                   \
} while(0)

#define DA_EXIT()                                                                       \
do {                                                                                    \
    if (da_debug_flag & DA_DEBUG_ENTRYEXIT_FLAG) {                                      \
        PRINT_LOG(LOG_INFO,"[EXIT]  :%s:%d\t: ", __FUNCTION__,__LINE__);                \
    }                                                                                   \
} while(0)


#define DA_INFO(msg, args...)                                                           \
do {                                                                                    \
    if (da_debug_flag & DA_DEBUG_INFO_FLAG) {                                           \
        PRINT_LOG(LOG_INFO,"[INFO]  :%s:%d\t: " msg, __FUNCTION__,__LINE__, ##args);    \
    }                                                                                   \
} while(0)


#define DA_WARNING(msg, args...)                                                        \
do {                                                                                    \
    if (da_debug_flag & DA_DEBUG_WARNING_FLAG) {                                        \
        PRINT_LOG(LOG_WARNING,"[WARN]  :%s:%d\t: " msg, __FUNCTION__,__LINE__, ##args); \
    }                                                                                   \
} while(0)

#define DA_ERROR(msg, args...)                                                          \
do {                                                                                    \
    if (da_debug_flag & DA_DEBUG_ERROR_FLAG) {                                          \
        PRINT_LOG(LOG_ERR,"[ERR]   :%s:%d\t: " msg, __FUNCTION__,__LINE__, ##args);     \
    }                                                                                   \
} while(0)

#define DA_ALERT(msg, args...)                                                          \
do {                                                                                    \
    if (da_debug_flag & DA_DEBUG_ALERT_FLAG) {                                          \
        PRINT_LOG(LOG_ALERT,"[ALERT] :%s:%d\t: " msg, __FUNCTION__,__LINE__, ##args);   \
    }                                                                                   \
} while(0)





#endif//__DA_DEBUG_H__
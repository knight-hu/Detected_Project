#ifndef __SUSPEND_INFO_H_
#define __SUSPEND_INFO_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define CMD_GET_STATUS	_IOR(0xFF, 123, unsigned char) 

#define DEV_NAME "/proc/usb_monitor"
//#define DEV_NAME "/mnt/test/data"

#define MAX_EPOLL_EVENTS         1
#define KERNEL_DATA_LENG       128 //读取数据的长度
#define MONITOR_DISABLE       0x00
#define MONITOR_ENABLE        0xff

size_t BUFFER_SIZE = 512;  //环形缓冲区长度

/*
128 bytes in total
struct usb_message_t
{
            s64 kernel_time;                 8 bytes                
            struct timeval timeval_utc;     16 bytes                   
            enum MESSAGE_TYPE status_old;    4 bytes 
            enum MESSAGE_TYPE status_new;    4 bytes     
            char 	product[32];			 32 bytes 
            char 	manufacturer[32];		 32 bytes 
            char 	serial[32];				 32 bytes            
};
*/
struct DataInfo{
    uint8_t kernel_time[8];       //8 Byte 
    uint8_t utc_time[16];          //16 byte
    uint8_t status_old[4];        //4 byte
    uint8_t status_new[4];        //4 byte
    char product[32];
    char manufacturer[32];
    char serial[32];
};

class UsbMonitorInfo {
public:
    struct DataInfo info;
};

#endif

#include <cinttypes>
#include <numeric>
#include <set>
#include <string>
#include <string.h>
#include <tuple>
#include <vector>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include "RingBuffer.h"
#include "UsbInfo.h"

using namespace std;

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  fifo_nonzero;
static volatile int64_t isEmpty = 0;
static volatile int64_t fifo_size = 0;

void printStatus(const char* desp, unsigned short status){
    //根据状态输出指定字符串，输出\033[1;34m为蓝色
    char s[]="\0USB DEVICE ADD\0USB DEVICE REMOVE";     //可以将字符串都放在一起，靠'\0'定位
    switch(status){
        case 0: //STATUS_NONE
            printf("%s = \033[1;34m%s\n\033[0m",desp,s);
            break;
        case 1: //USB_ADD
            printf("%s = \033[1;34m%s\n\033[0m",desp,&s[1]);
            break;
        case 2: //USB_REMOVE
            printf("%s = \033[1;34m%s\n\033[0m",desp,&s[16]);
            break;
        default:
            break;
    }
}


class UsbMonitorDevice {
public:
    UsbMonitorDevice(char* name){
        mDev_name = name;
    }
    ~UsbMonitorDevice(){
        epoll_ctl(mEpollfd, EPOLL_CTL_DEL, mFd, &mEpev);
        close(mEpollfd);
        close(mFd);
    }

    int InitSetup(){
        //open "/proc/usb_monitor"
        mFd = open(mDev_name, O_RDWR); 
        if (mFd == -1) {
			printf("open %s fail, Check!!!\n",mDev_name);
            return errno;
        }
        //epoll
        mEpollfd = epoll_create(MAX_EPOLL_EVENTS);
        if (mEpollfd == -1) {
			printf("epoll_create failed errno = %d ", errno);	
            return errno;
        }
		printf("epoll_create ok epollfd= %d \n", mEpollfd);
		
        //add fd for epoll
        memset(&mEpev, 0, sizeof(mEpev));
	    mEpev.data.fd = mFd;
	    mEpev.events = EPOLLIN;
        if (epoll_ctl(mEpollfd, EPOLL_CTL_ADD, mFd, &mEpev) < 0) {
			printf("epoll_ctl failed, errno = %d \n", errno);
            return errno;
        }  
        return 0;
    }
 
    int getFd() { return mFd; };
    int getepollfd() { return mEpollfd; };
    char* getBuffer() { return mBuf; };

    UsbMonitorInfo& GetFristDataInfo(){
        return mRingBuffer.Get(0);
    }
    void PPopFrontDatainfo(){
        mRingBuffer.PopFront();
    }

    void PopBackDatainfo(){
        mRingBuffer.PopBack();
    }

    UsbMonitorInfo& GetBackDataInfo(){
        return mRingBuffer.Back();
    }

    void AppendDatainfo(UsbMonitorInfo& info){
        mRingBuffer.Append(info);
    }

    size_t GetFifoSize(){
        return mRingBuffer.GetSize();
    }

    void FifoReset(size_t capacity) {
        mRingBuffer.Reset(capacity);
    }

    bool FifoIsEmpty() { 
        return mRingBuffer.IsEmpty();
    }

private:
    int mFd; //"/proc/usb_monitor"
    int mEpollfd;
    struct epoll_event mEpev;
    char *mDev_name;
    char mBuf[256];
    RingBuffer<UsbMonitorInfo> mRingBuffer;
};

static void * DoUsbMonitor(void *arg){
        int ret;
        //static int index = 0;
        ssize_t leng = 0, i = 0;
        struct epoll_event epev;
        UsbMonitorInfo deviceinfo;
        UsbMonitorDevice* device = (UsbMonitorDevice*)arg;
        char* buf = device->getBuffer();
        device->FifoReset(BUFFER_SIZE);

        //定义读取一定字节数据的指针
        unsigned long long* ns;     //kernal纳秒数
        unsigned long long* utc_s;  //ts格式的时间，秒数
        unsigned long long* utc_ns; //ts格式的时间，纳秒数
        unsigned short* status_old; //上一次的状态
        unsigned short* status_new; //这一次的状态

        while(1){
            printf("usb_monitor epoll_wait... \n");
            ret = epoll_wait(device->getepollfd(), &epev, MAX_EPOLL_EVENTS, -1);
            if (ret == -1 && errno != EINTR) {
				printf("usb_monitor epoll_wait failed; errno=%d\n", errno);
                return (void*)-1;
            }
            leng  = read(device->getFd(), buf, KERNEL_DATA_LENG); //MAX 32 Byte
            if (leng == KERNEL_DATA_LENG){
                //8 byte kernel time
                for (i = 0; i < 8; i++){
                    deviceinfo.info.kernel_time[i] = buf[i];
                }
                //16 byte UTC
                 for (i = 0; i < 16; i++){
                    deviceinfo.info.utc_time[i] =  buf[i+8];
                }
                //4 byte status_old
                for (i = 0; i < 4; i++){
                    deviceinfo.info.status_old[i] = buf[i+24];
                }
                //4 byte status_new
                for (i = 0; i < 4; i++){
                    deviceinfo.info.status_new[i] = buf[i+28];
                }
                //32 byte product info
                for (i=0; i<32;i++){
                    deviceinfo.info.product[i]=buf[i+32];
                }
                //32 byte manufacturer info
                for(i=0;i<32;i++){
                    deviceinfo.info.manufacturer[i]=buf[i+64];
                }
                //32 byte serial info
                for(i=0;i<32;i++){
                    deviceinfo.info.serial[i]=buf[i+96];
                }
                
                //使用强制类型转换获取可读的数据
                ns=(unsigned long long*)deviceinfo.info.kernel_time;
                utc_s = (unsigned long long*)deviceinfo.info.utc_time;
                utc_ns = (unsigned long long*)&deviceinfo.info.utc_time[8];
                //\033[0;32m 绿色
                //\033[0;33m 黄色
                //\033[0;36m 青蓝色
                printf("\033[0;33mkernel_time\033[0m\t\033[0;36m%llu\n\033[0m",*(unsigned long long*)ns);    
                printf("\033[0;33mutc_time\033[0m\t\033[0;36m%llu.%llu\n\033[0m",*(unsigned long long*)utc_s,*(unsigned long long*)utc_ns);
                
                status_old=(unsigned short*)deviceinfo.info.status_old;
                status_new=(unsigned short*)deviceinfo.info.status_new;
                //输出状态信息
                printStatus("\033[1;33mstatus_old\033[0m",*status_old);
                printStatus("\033[1;33mstatus_new\033[0m",*status_new);
                //输出usb信息
                printf("\033[1;33mUSB-Name: \033[0;32m%s\n\033[0m",deviceinfo.info.product);
                printf("\033[1;33mUSB-Manufacturer: \033[0;32m%s\n\033[0m",deviceinfo.info.manufacturer);
                printf("\033[1;33mUSB-Serial: \033[0;32m%s\n\033[0m",deviceinfo.info.serial);

                ret = pthread_mutex_lock(&data_mutex); //get lock
		        if (ret != 0) {
			        printf("Error on pthread_mutex_lock(), ret = %d\n", ret);
			        return (void *)-1;
		        }

                //save 
                device->AppendDatainfo(deviceinfo);
                fifo_size = device->GetFifoSize();
                
                ret = pthread_mutex_unlock(&data_mutex); //unlock
		        if (ret != 0) {
			        printf("Error on pthread_mutex_unlock(), ret = %d\n", ret);
			        return (void *)-1;
		        }

                if (isEmpty){
                    for (i = 0; i < isEmpty; i++)
                        pthread_cond_signal(&fifo_nonzero);
                }
                printf("\033[1;33mCurrent BufferSize\033[0m = %ld \n", fifo_size);
            }

        }
}

int main(){
    int ret; 
    pthread_t ptd;
    //check device
    UsbMonitorDevice* monitorDevice = new UsbMonitorDevice((char*)DEV_NAME);
    if ( monitorDevice->InitSetup() != 0){
		printf("UsbMonitorDevice::InitSetup fail \n");
        return -1;
    }
	printf("UsbMonitorDevice::InitSetup OK \n");
    
    DoUsbMonitor((void*)monitorDevice);

    pthread_mutex_destroy(&data_mutex);
    //not call here
    return 0;
}
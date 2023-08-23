#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

#include <linux/fb.h>
#include <linux/usb.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/syscore_ops.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/mutex.h>


#define LOGI(...)	(pr_info(__VA_ARGS__))

#define LOGE(...)	(pr_err(__VA_ARGS__))

#define MESSAGE_BUFFER_SIZE	5

#define CMD_GET_STATUS	_IOR(0xFF, 123, unsigned char)

// USB状态
enum STATUS_TYPE {
	STATUS_NONE=0,
	USB_ADD = 1,
	USB_REMOVE = 2
};

/* 128 bytes in total*/
struct usb_message_t {
	signed long long kernel_time;			/* 8 bytes */
	struct	timespec64 timeval_utc;			/* 16 bytes	*/
	enum	STATUS_TYPE status_old;			/* 4 bytes */
	enum	STATUS_TYPE status_new;			/* 4 bytes */
	char 	product[32];					/* 32 bytes */
	char 	manufacturer[32];				/* 32 bytes */
	char 	serial[32];						/* 32 bytes */
};

struct usb_monitor_t {
	struct	notifier_block usb_notif;
	struct	usb_message_t message[MESSAGE_BUFFER_SIZE];
	int		usb_message_count;
	int		usb_message_index_read;
	int		usb_message_index_write;
	char	write_buff[10];
	int		enable_usb_monitor;
	wait_queue_head_t	usb_monitor_queue;
	struct	mutex usb_monitor_mutex;
};

static struct usb_monitor_t *monitor;
static char *TAG = "MONITOR";

//被读取时调用，向buf中填充usb_message_t消息
static ssize_t usb_monitor_read(struct file *filp, char __user *buf,
				size_t size, loff_t *ppos)
{
	int index;
	size_t message_size = sizeof(struct usb_message_t);

	LOGI("%s:%s\n", TAG, __func__);

	if (size < message_size) {
		LOGE("%s:read size is smaller than message size!\n", TAG);
		return -EINVAL;
	}

	wait_event_interruptible(monitor->usb_monitor_queue,
		monitor->usb_message_count > 0);

	LOGI("%s:read wait event pass\n", TAG);

	mutex_lock(&monitor->usb_monitor_mutex);

	if (monitor->usb_message_count > 0) {
		index = monitor->usb_message_index_read;

		if (copy_to_user(buf, &monitor->message[index], message_size)) {
			LOGE("%s:copy_from_user error!\n", TAG);
			mutex_unlock(&monitor->usb_monitor_mutex);
			return -EFAULT;
		}

		monitor->usb_message_index_read++;
		if (monitor->usb_message_index_read >= MESSAGE_BUFFER_SIZE)
			monitor->usb_message_index_read = 0;

		monitor->usb_message_count--;
	}

	mutex_unlock(&monitor->usb_monitor_mutex);

	LOGI("%s:read count:%d\n", TAG, message_size);

	return message_size;
}
//被写入时调用，从buf向usb_monitor_t.write_buff写入数据
static ssize_t usb_monitor_write(struct file *filp, const char __user *buf,
				size_t size, loff_t *ppos)
{
	char end_flag = 0x0a, cmd;

	LOGI("%s:%s\n", TAG, __func__);

	/* only support size=2, such as "echo 0 > usb_monitor" */
	if (size != 2) {
		LOGE("%s:invalid cmd size: size = %d\n", TAG, (int)size);
		return -EINVAL;
	}

	if (copy_from_user(monitor->write_buff, buf, size)) {
		LOGE("%s:copy_from_user error!\n", TAG);
		return -EFAULT;
	}

	if (monitor->write_buff[1] != end_flag) {
		LOGE("%s:invalid cmd: end_flag != 0x0a\n", TAG);
		return -EINVAL;
	}

	cmd = monitor->write_buff[0];

	mutex_lock(&monitor->usb_monitor_mutex);

	switch (cmd) {
	case '0':
		monitor->enable_usb_monitor = 0;
		LOGI("%s:disable usb monitor\n", TAG);
		break;
	case '1':
		monitor->enable_usb_monitor = 1;
		LOGI("%s:enable usb monitor\n", TAG);
		break;
	default:
		LOGE("%s:invalid cmd: cmd = %d\n", TAG, cmd);
		mutex_unlock(&monitor->usb_monitor_mutex);
		return -EINVAL;
	}

	mutex_unlock(&monitor->usb_monitor_mutex);

	return size;
}

static unsigned int usb_monitor_poll(struct file *filp,
						struct poll_table_struct *wait)
{
	unsigned int mask = 0;

	LOGI("%s:%s\n", TAG, __func__);

	poll_wait(filp, &monitor->usb_monitor_queue, wait);

	mutex_lock(&monitor->usb_monitor_mutex);

	if (monitor->usb_message_count > 0)
		mask |= POLLIN | POLLRDNORM;

	mutex_unlock(&monitor->usb_monitor_mutex);

	return mask;
}

static long usb_monitor_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	void __user *ubuf = (void __user *)arg;
	unsigned char status;

	LOGI("%s:%s\n", TAG, __func__);

	mutex_lock(&monitor->usb_monitor_mutex);

	switch (cmd) {
	case CMD_GET_STATUS:
		LOGI("%s:ioctl:get enable status\n", TAG);
		if (monitor->enable_usb_monitor == 0)
			status = 0x00;
		else
			status = 0xff;

		LOGI("%s:ioctl:status=0x%x\n", TAG, status);

		if (copy_to_user(ubuf, &status, sizeof(status))) {
			LOGE("%s:ioctl:copy_to_user fail\n", TAG);
			mutex_unlock(&monitor->usb_monitor_mutex);
			return -EFAULT;
		}
		break;
	default:
		LOGE("%s:invalid cmd\n", TAG);
		mutex_unlock(&monitor->usb_monitor_mutex);
		return -ENOTTY;
	}

	mutex_unlock(&monitor->usb_monitor_mutex);

	return 0;
}
//回调函数集合，新版内核写法
static const struct proc_ops usb_monitor_fops = {
	.proc_read = usb_monitor_read,
	.proc_write = usb_monitor_write,
	.proc_poll = usb_monitor_poll,
	.proc_ioctl = usb_monitor_ioctl,
};
//写消息，主动调用，向usb_monitor_t.message写入信息
static void write_message(enum STATUS_TYPE status_new, struct usb_device* usb)
{
	enum STATUS_TYPE status_old;
	int index;

	LOGI("%s:%s\n", TAG, __func__);

	mutex_lock(&monitor->usb_monitor_mutex);

	index = monitor->usb_message_index_write;
	status_old = monitor->message[index].status_new;

	monitor->usb_message_index_write++;
	if (monitor->usb_message_index_write >= MESSAGE_BUFFER_SIZE)
		monitor->usb_message_index_write = 0;

	index = monitor->usb_message_index_write;

	monitor->message[index].kernel_time = ktime_to_ns(ktime_get());
	ktime_get_ts64(&monitor->message[index].timeval_utc);
	monitor->message[index].status_old = status_old;
	monitor->message[index].status_new = status_new;

	int i=0;

	//先向usb各种信息中写入null，若usb某项信息缺失，则可以正确返回(null)字符串
	char nothing[]="(null)";
	for (i=0;nothing[i]!=0;i++){
		monitor->message[index].product[i]=nothing[i];
		monitor->message[index].manufacturer[i]=nothing[i];
		monitor->message[index].serial[i]=nothing[i];
	}
	//结束字符'\0'也要写上
	monitor->message[index].product[i]='\0';
	monitor->message[index].manufacturer[i]='\0';
	monitor->message[index].serial[i]='\0';

	//判断各个信息中是否有内容，若有则写入
	if(usb->product!=NULL){
		for (i=0;i<32;i++){
			//产品名称
			monitor->message[index].product[i]=usb->product[i];
			if (usb->product[i]=='\0') break;
		}
	}
	if(usb->manufacturer!=NULL){
		for (i=0;i<32;i++){
			//制造厂商
			monitor->message[index].manufacturer[i]=usb->manufacturer[i];
			if(usb->manufacturer[i]=='\0') break;
		}
	}
	if(usb->serial!=NULL){
		for (i=0;i<32;i++){
			//序列号
			monitor->message[index].serial[i]=usb->serial[i];
			if(usb->serial[i]=='\0') break;
		}
	}

	if (monitor->usb_message_count < MESSAGE_BUFFER_SIZE)
		monitor->usb_message_count++;

	wake_up_interruptible(&monitor->usb_monitor_queue);

	mutex_unlock(&monitor->usb_monitor_mutex);
}

//当出现usb事件时被调用
static int usbdev_notify(struct notifier_block* self, unsigned long action, void* data){
	//强制类型转换，获取data中usb_device数据
	struct usb_device* usb_dev = (struct usb_device *) data;

	LOGI("%s:%s\n", TAG, __func__);

	mutex_lock(&monitor->usb_monitor_mutex);

	if (monitor->enable_usb_monitor == 0) {
		LOGE("%s:usb monitor is disable\n", TAG);
		mutex_unlock(&monitor->usb_monitor_mutex);
		return 0;
	}

	mutex_unlock(&monitor->usb_monitor_mutex);

	switch (action) {
	case USB_DEVICE_ADD:	//USB设备被添加
		LOGI("%s:USB_DEVICE_ADD\n", TAG);
		write_message(USB_ADD,usb_dev);
		break;

	case USB_DEVICE_REMOVE:	//USB设备被移除
		LOGI("%s:USB_DEVICE_REMOVE\n", TAG);
		write_message(USB_REMOVE,usb_dev);
		break;

	default:
		break;
	}

	return 0;
}

static int __init usb_monitor_init(void)
{
	int i;

	LOGI("%s:%s\n", TAG, __func__);

	monitor = kzalloc(sizeof(struct usb_monitor_t), GFP_KERNEL);

	if (!monitor) {
		LOGE("%s:failed to kzalloc\n", TAG);
		return -ENOMEM;
	}
	//初始化消息
	for (i = 0; i < MESSAGE_BUFFER_SIZE; i++) {
		monitor->message[i].status_old = STATUS_NONE;
		monitor->message[i].status_new = STATUS_NONE;
	}
	monitor->usb_message_count = 0;
	monitor->usb_message_index_read = 1;
	monitor->usb_message_index_write = 0;
	monitor->enable_usb_monitor = 1;
	
	//提供设备读写
	proc_create("usb_monitor", 0644, NULL, &usb_monitor_fops);

	init_waitqueue_head(&monitor->usb_monitor_queue);

	mutex_init(&monitor->usb_monitor_mutex);

	//设置回调函数
	monitor->usb_notif.notifier_call = usbdev_notify;
	//注册usb通知
	usb_register_notify(&monitor->usb_notif);
	return 0;
}

static void __exit usb_monitor_exit(void)
{
	LOGI("%s:%s\n", TAG, __func__);

	remove_proc_entry("usb_monitor", NULL);

	//解注册usb
	usb_unregister_notify(&monitor->usb_notif);

	kfree(monitor);
}

module_init(usb_monitor_init);
module_exit(usb_monitor_exit);

MODULE_AUTHOR("MediaTek");
MODULE_DESCRIPTION("Usb Monitor");
MODULE_LICENSE("GPL v2");

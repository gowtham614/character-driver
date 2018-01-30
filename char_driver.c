#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define MYDEV_NAME "mycdrv"
#define ramdisk_size (size_t) (16*PAGE_SIZE)

#define CDRV_IOC_MAGIC 'k'
#define ASP_CLEAR_BUFF _IO(CDRV_IOC_MAGIC, 1)

/* parameters */
static int NUM_DEVICES = 3;

struct ASP_mycdrv {
	struct cdev dev;
	char *ramdisk;
	struct semaphore sem;
	int devNo;
	struct list_head list;
	ssize_t size;
} mylist;

module_param(NUM_DEVICES, int, S_IRUSR | S_IWUSR);

struct class *dev_class = NULL;

static struct ASP_mycdrv *my_devices = NULL;
static unsigned int major_number = 0;

int mycdrv_open(struct inode *inode, struct file *filp) {
	unsigned int minor = iminor(inode);
	struct list_head *pos;
	struct ASP_mycdrv *present_Device = NULL;
	list_for_each(pos, &(mylist.list))
	{
		present_Device = list_entry(pos, struct ASP_mycdrv, list);
		if (present_Device->devNo == minor)
			break;
	}
	filp->private_data = present_Device;
	return 0;
}

int mycdrv_release(struct inode *inode, struct file *filp) {
	return 0;
}

ssize_t mycdrv_read(struct file *filp, char __user *bufStoreData, size_t bufCount, loff_t *curOffset)
{
	struct ASP_mycdrv *dev = filp->private_data;
	ssize_t nbytes = 0;

	if (down_interruptible(&dev->sem))
	return -EINTR;

	if ( (*curOffset + bufCount >= dev->size) || (*curOffset < 0)  ) {
		up(&dev->sem);
		return -EINTR;
	}
	else {

		nbytes = bufCount - copy_to_user(bufStoreData, &(dev->ramdisk[*curOffset]), bufCount);
		*curOffset += nbytes;
		//msleep(6000);
		up(&dev->sem);
		return nbytes;
	}
}

ssize_t mycdrv_write(struct file *filp, const char __user *bufStoreData, size_t bufCount, loff_t *curOffset)
{
	struct ASP_mycdrv *dev = filp->private_data;
	ssize_t nbytes = 0;

	if (down_interruptible(&dev->sem))
	return -EINTR;

	if ((*curOffset + bufCount >= dev->size) || (*curOffset < 0)) {
		up(&dev->sem);
		return -EINTR;
	}
	else {
		nbytes = bufCount - copy_from_user(&(dev->ramdisk[*curOffset]), bufStoreData, bufCount);
		*curOffset += nbytes;
		//msleep(10000);

		up(&dev->sem);
		return bufCount;
	}
}

static long mycdrv_ioctl(struct file *filp, unsigned int id,
		unsigned long param) {
	struct ASP_mycdrv *dev = filp->private_data;

	switch (id) {
	case ASP_CLEAR_BUFF:
		down(&(dev->sem));
		memset(dev->ramdisk, 0, dev->size);
		filp->f_pos = 0;

		up(&(dev->sem));
		return 0;
		break;
	default:
		return -ENOTTY;
		break;
	}

}

loff_t mycdrv_llseek(struct file *filp, loff_t offset, int whence) {
	struct ASP_mycdrv *dev = filp->private_data;
	loff_t testpos, newsize;

	if (down_interruptible(&dev->sem))
		return -EINTR;

	switch (whence) {
	case SEEK_SET:
		testpos = offset;
		break;

	case SEEK_CUR:
		testpos = filp->f_pos + offset;
		break;

	case SEEK_END:
		testpos = dev->size + offset;
		break;

	default:
		up(&dev->sem);
		return -EINVAL;
	}
	//printk(KERN_ALERT "ALERT##: testpos = %lld", testpos);
	if (testpos >= 0) {
		//printk(KERN_ALERT "ALERT##: in if section");
		if (testpos > dev->size) {
			newsize = testpos;
			dev->ramdisk = krealloc(dev->ramdisk, newsize, GFP_KERNEL);
			memset(dev->ramdisk + ramdisk_size, 0, newsize - dev->size);
			dev->size = newsize;
		}
		filp->f_pos = testpos;
		up(&dev->sem);
		return testpos;

	} else {
		//printk(KERN_ALERT "ALERT##: in else section");
		up(&dev->sem);
		return -EINVAL;
	}

}

struct file_operations fops = { .owner = THIS_MODULE, .read = mycdrv_read,
		.write = mycdrv_write, .open = mycdrv_open, .release = mycdrv_release,
		.llseek = mycdrv_llseek, .unlocked_ioctl = mycdrv_ioctl, };

static int my_init(void) {
	int err = 0;
	int i = 0;
	dev_t dev = 0, dev_no;
	struct device *device;
	err = alloc_chrdev_region(&dev, 0, NUM_DEVICES, MYDEV_NAME);
	major_number = MAJOR(dev);

	dev_class = class_create(THIS_MODULE, MYDEV_NAME);
	INIT_LIST_HEAD(&mylist.list);
	for (i = 0; i < NUM_DEVICES; ++i) {
		my_devices = (struct ASP_mycdrv *) kzalloc(sizeof(struct ASP_mycdrv),
				GFP_KERNEL);
		INIT_LIST_HEAD(&my_devices->list);
		list_add(&(my_devices->list), &(mylist.list));
		dev_no = MKDEV(major_number, i);
		my_devices->ramdisk = kzalloc(ramdisk_size, GFP_KERNEL);
		my_devices->size = ramdisk_size;
		//memset(my_devices->ramdisk, 0, ramdisk_size);
		my_devices->devNo = i;
		sema_init(&my_devices->sem, 1);
		cdev_init(&my_devices->dev, &fops);
		my_devices->dev.owner = THIS_MODULE;
		cdev_add(&my_devices->dev, dev_no, 1);
		device = device_create(dev_class, NULL, dev_no, NULL, "%s%d",
		MYDEV_NAME, i);
	}
	printk(KERN_ALERT "newTestDevice: intialized the devices");

	return 0;
}

static void my_exit(void) {
	int i = 0;
	struct list_head *pos, *q;
	struct ASP_mycdrv *present_Device = NULL;
	list_for_each(pos, &(mylist.list))
	{
		present_Device = list_entry(pos, struct ASP_mycdrv, list);
		if (present_Device && present_Device->ramdisk) {
			kfree(present_Device->ramdisk);
		}
		cdev_del(&(present_Device->dev));
		device_destroy(dev_class, MKDEV(major_number, i));
		i++;
	}
	class_unregister(dev_class);
	if (dev_class)
		class_destroy(dev_class);

	unregister_chrdev_region(MKDEV(major_number, 0), NUM_DEVICES);
	list_for_each_safe(pos, q, &(mylist.list))
	{
		present_Device = list_entry(pos, struct ASP_mycdrv, list);
		list_del(pos);
		kfree(present_Device);
	}
}

module_init( my_init);
module_exit( my_exit);

MODULE_AUTHOR("Gowtham");
MODULE_LICENSE("GPL v2");

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>

extern int read_digit_from_7seg(void);

extern int write_digit_to_7seg(int digit);


#define DEVICE_NAME "7seg_lkm"
#define BUF_SIZE 512


int printed_digit = -1;


enum state {
    low, high
};


const unsigned char mask[10][7] = {
        //5, 17, 22, 23, 24, 25, 27
        {1, 1, 0, 1, 1, 1, 1},
        {0, 0, 0, 1, 0, 0, 1},
        {0, 1, 1, 0, 1, 1, 1},
        {1, 1, 1, 0, 1, 1, 0},
        {1, 0, 1, 1, 1, 0, 0},
        {1, 1, 1, 1, 0, 1, 0},
        {1, 1, 1, 1, 0, 1, 1},
        {1, 0, 0, 0, 1, 1, 0},
        {1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 0}
};

const unsigned int symbols[7] = {5, 17, 22, 23, 24, 25, 27};


struct sevenseg_lkm_dev {

    struct cdev cdev;

};


static int sevenseg_lkm_open(struct inode *inode, struct file *filp);

static int sevenseg_lkm_release(struct inode *inode, struct file *filp);

static ssize_t sevenseg_lkm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);

static ssize_t sevenseg_lkm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);


static struct file_operations sevenseg_lkm_fops =
        {
                .owner = THIS_MODULE,
                .open = sevenseg_lkm_open,
                .release = sevenseg_lkm_release,
                .read = sevenseg_lkm_read,
                .write = sevenseg_lkm_write,
        };


static int sevenseg_lkm_init(void);

static void sevenseg_lkm_exit(void);


struct sevenseg_lkm_dev *sevenseg_lkm_devp;


static dev_t first;

static struct class *sevenseg_lkm_class;


static int sevenseg_lkm_open(struct inode *inode, struct file *filp) {
    struct sevenseg_lkm_dev *sevenseg_lkm_devp;


    printk(KERN_INFO
           "[7SEG-LKM] -  opened\n");

    sevenseg_lkm_devp = container_of(inode->i_cdev,
                                     struct sevenseg_lkm_dev, cdev);

    filp->private_data = sevenseg_lkm_devp;


    return 0;
}


static int sevenseg_lkm_release(struct inode *inode, struct file *filp) {


    filp->private_data = NULL;
    /* print debug message to system journal - good practice */
    printk(KERN_INFO
           "[GPIO-LKM] - Closing\n");

    return 0;
}


extern int read_digit_from_7seg(void) {
    return printed_digit;
}

EXPORT_SYMBOL (read_digit_from_7seg);

static ssize_t sevenseg_lkm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
    ssize_t retval;
    char byte;


    for (retval = 0; retval < count; ++retval) {

        byte = '0' + printed_digit;


        if (put_user(byte, buf + retval))
            break;
    }

    return retval;
}


extern int write_digit_to_7seg(int digit) {
    int i;
    if (digit > 9 || digit < -1) {
        return -1;
    }
    printed_digit = digit;

    if (digit == -1) {
        for (i = 0; i < 7; i++) {
            gpio_set_value(symbols[i], low);
        }
        return 0;
    }


    for (i = 0; i < 7; ++i) {
        if (mask[digit][i])
            gpio_set_value(symbols[i], high);
        else
            gpio_set_value(symbols[i], low);
    }
    return 0;
}

EXPORT_SYMBOL(write_digit_to_7seg);


static ssize_t sevenseg_lkm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
    unsigned int len = 0, i;
    char kbuf[BUF_SIZE];

    len = count < BUF_SIZE ? count - 1 : BUF_SIZE - 1;

    if (raw_copy_from_user(kbuf, buf, len) != 0)
        return -EFAULT;

    kbuf[len] = '\0';
    if (len > 1) {
        for (i = 0; i < 7; ++i)
            gpio_set_value(symbols[i], low);
        printed_digit = -1;

        printk(KERN_INFO
               "[7SEG-LKM] - cannot set 7seg to %s\n", kbuf);
        return -1;
    }
    if (write_digit_to_7seg(buf[0] - '0')) {
        printk("[7SEG-LKM] - Unable to write the specified digit %c", buf[0]);
        return -1;
    }

    printed_digit = buf[0] - '0';

    *f_pos += count;
    return count;
}


static int __init

sevenseg_lkm_init(void) {
    int i, ret, index = 0;

    if (alloc_chrdev_region(&first, 0, 1, DEVICE_NAME) < 0) {
        printk(KERN_DEBUG
               "Cannot register device\n");
        return -1;
    }


    if ((sevenseg_lkm_class = class_create(THIS_MODULE, DEVICE_NAME)) == NULL) {
        printk(KERN_DEBUG
               "Cannot create class %s\n", DEVICE_NAME);

        unregister_chrdev_region(first, 1);

        return -EINVAL;
    }

    sevenseg_lkm_devp = kmalloc(sizeof(struct sevenseg_lkm_dev), GFP_KERNEL);

    if (!sevenseg_lkm_devp) {
        printk(KERN_DEBUG
               "[7SEG-LKM]: Bad kmalloc\n");
        return -ENOMEM;
    }

    for (index = 0; index < 7; ++index)
        if (gpio_request_one(symbols[index], GPIOF_OUT_INIT_LOW, NULL) < 0) {
            printk(KERN_ALERT
                   "[7SEG-LKM] - Error requesting GPIO %d\n", i);
            return -ENODEV;
        }

    printed_digit = -1;
    sevenseg_lkm_devp->cdev.owner = THIS_MODULE;
    cdev_init(&sevenseg_lkm_devp->cdev, &sevenseg_lkm_fops);

    if ((ret = cdev_add(&sevenseg_lkm_devp->cdev, first, 1))) {
        printk(KERN_ALERT
               "[7SEG-LKM] - Error %d adding cdev\n", ret);


        device_destroy(sevenseg_lkm_class,
                       MKDEV(MAJOR(first),
                             MINOR(first)));


        /* clean up in opposite way from init
         */
        class_destroy(sevenseg_lkm_class);
        unregister_chrdev_region(first, 1);
        return ret;
    }

    if (device_create(sevenseg_lkm_class, NULL, MKDEV(MAJOR(first), MINOR(first)), NULL, "7seg") == NULL) {

        class_destroy(sevenseg_lkm_class);
        unregister_chrdev_region(first, 1);

        return -1;
    }


    printk("[7SEG-LKM] - Driver initialized\n");

    return 0;
}


static void __exit

sevenseg_lkm_exit(void) {
    int i = 0;

    unregister_chrdev_region(first, 1);
    kfree(sevenseg_lkm_devp);


    device_destroy(sevenseg_lkm_class, MKDEV(MAJOR(first), MINOR(first) + i));
    for (; i < 7; ++i)
        gpio_free(symbols[i]);
    /* destroy class
     */
    class_destroy(sevenseg_lkm_class);
    printk(KERN_INFO
           "[7SEG-LKM] - Raspberry Pi GPIO driver removed\n");
}

/* these are stantard macros to mark
 * init and exit functions implemetations
 */
module_init(sevenseg_lkm_init);
module_exit(sevenseg_lkm_exit);

MODULE_LICENSE("GPL");

MODULE_AUTHOR("Denys Datsko, Nazar Pasternak, Sofia Petryshyn");

MODULE_DESCRIPTION("7seg Loadable Kernel Module - Linux device driver for Raspberry Pi for 7 segment display");

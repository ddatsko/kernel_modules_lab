/*
* gpio_lkm_driver.c - GPIO Loadable Kernel Module
* Implementation - Linux device driver for Raspberry Pi
* Author: Roman Okhrimenko <mrromanjoe@gmail.com>
* Version: 1.0
* License: GPL
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>

/*disclaimer: not all of Raspberry pins
 * are available to be used as GPIOs */

extern int read_digit_from_7seg(void);
extern int write_digit_to_7seg(int digit);


#define DEVICE_NAME "gpio_lkm" /* name that will be assigned to this device in /dev fs */
#define BUF_SIZE 512
#define NUM_COM 4 /* number of commands that this driver support */


int printed_digit = -1;


/* buffer with set of supported commands */
const char *commands[NUM_COM] = {"out", "in", "low", "high"};
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

/* enumerators to match commands with values for following processing */
enum commands {
    set_out = 0,
    set_in = 1,
    set_low = 2,
    set_high = 3,
    na = NUM_COM + 1
};

enum direction {
    in, out
};
enum state {
    low, high
};

/*
* struct gpio_lkm_dev - Per gpio pin data structure
* @cdev: instance of struct cdev
* @pin: instance of struct gpio
* @state: logic state (low, high) of a GPIO pin
* @dir: direction of a GPIO pin
*/

struct gpio_lkm_dev {
    /* declare struct cdev that will represent
     * our char device in inode structure. 
     * inode is used by kernel to represent file objects */
    struct cdev cdev;

};

/* to implement a char device driver we need to satisfy some
 * requirements. one of them is an implementation of mandatory
 * methods defined in struct file_operations
 *  - read
 *  - write
 *  - open
 *  - release
 * think about device as about a simple file. these are basic
 * operations you do with all regular files. same for char device
 * signatures of functions defined in linux/include/fs.h may be
 * called a virtual methods in OOP terminology, that you need to
 * implemt. all these are repsented as callback functions
 */
static int gpio_lkm_open(struct inode *inode, struct file *filp);

static int gpio_lkm_release(struct inode *inode, struct file *filp);

static ssize_t gpio_lkm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);

static ssize_t gpio_lkm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);

/* declare structure gpio_lkm_fops which holds 
 * our implementations of callback functions,
 * in instance of struct file_operations for our
 * char device driver
 */
static struct file_operations gpio_lkm_fops =
        {
                .owner = THIS_MODULE,
                .open = gpio_lkm_open,
                .release = gpio_lkm_release,
                .read = gpio_lkm_read,
                .write = gpio_lkm_write,
        };

/* declare prototypes of init and exit functions.
 * implementation of these 2 functions is mandatory
 * for each linux kernel module. they serve to
 * satisfy kernel request to initialize module
 * when you insmod'ing it in system and release
 * allocated resources when you prform rmmod command */
static int gpio_lkm_init(void);

static void gpio_lkm_exit(void);

/* declare an array of gpio_lkm_dev device structure objects
 * which represent each of our pins as a char device */
struct gpio_lkm_dev *gpio_lkm_devp;
/* */
static dev_t first;
/* declare pointer to our device class. this will
 * be used to satisfy udev kernel service requirements.
 * udev service automatically creates devices in /dev
 * filesystem and populates /sysfs filesystem with
 * values provided by drivers when calling corresponding APIs
 */
static struct class *gpio_lkm_class;

/*
* which_command - used to decode string reprecentations of
* command received from user space request via write request.
*/
static unsigned int which_command(const char *com) {
    unsigned int i;

    for (i = 0; i < NUM_COM; i++) {
        if (!strcmp(com, commands[i]))
            return i;
    }
    return na;
}

/* comprehensive reading about read/write/open/release char device methods
*  https://www.oreilly.com/library/view/linux-device-drivers/0596005903/ch03.html
*/

/*
* gpio_lkm_open - Open GPIO device
* this is implementation of previously declared function
* open from our file_operations structure.
* this function will be called each time device recives
* open file operation applied to its name in /dev
* open method call creates struct file instance
*/
static int gpio_lkm_open(struct inode *inode, struct file *filp) {
    struct gpio_lkm_dev *gpio_lkm_devp;
    unsigned int gpio;

    /* call include/linux/fs.h api to get minor 
     * number from input inode struct */
    gpio = iminor(inode);
    /* print obtained number to system journal */
    printk(KERN_INFO
    "[GPIO-LKM] - GPIO[%d] opened\n", gpio);
    /* this macro basically tells kernel to match name cdev
     * of type struct gpio_lkm_dev to where first argument points to
     * see struct inode definition in fs.h line 679 and more 
     * info about macro here
     * https://radek.io/2012/11/10/magical-container_of-macro/
     *  */
    gpio_lkm_devp = container_of(inode->i_cdev,
    struct gpio_lkm_dev, cdev);
    /* assign a pointer to struct representing our 
     * device to its corresponding file object */
    filp->private_data = gpio_lkm_devp;

    /* zero returns stand for success in kernel programming */
    return 0;
}

/*
* gpio_lkm_release - Release GPIO device
* this function is called whenever kernel tries to remove driver
* module from system. here all 
*/
static int gpio_lkm_release(struct inode *inode, struct file *filp) {
    unsigned int gpio;

    gpio = iminor(inode);

    /* remove pointer our device data, that was assigned in open 
     * if any resources was allocated they should be dealocated
     * here before return
    */
    filp->private_data = NULL;
    /* print debug message to system journal - good practice */
    printk(KERN_INFO
    "[GPIO-LKM] - Closing GPIO %d\n", gpio);

    return 0;
}




extern int read_digit_from_7seg(void) {
    return printed_digit;
}

EXPORT_SYMBOL ( read_digit_from_7seg );

static ssize_t gpio_lkm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
    unsigned int gpio;
    ssize_t retval;
    char byte;
    struct gpio_lkm_dev *gpio_lkm_devp = filp->private_data;

    /* determine which device is read from inode sctructure.
     * remember, we have 14 GPIOs at max and each is effectively
     * separate device using same driver
     */
    gpio = iminor(filp->f_path.dentry->d_inode);

    /* get count amount of values from GPIO device */
    for (retval = 0; retval < count; ++retval) {
        /* use kernel gpio API functions to get
         * value of gpio by minor numer of device
         */
        byte = '0' + printed_digit;

        /* use special macro to copy data from kernel space
         * co user space. API related to user space
         * interactions are found in arm/asm/uaccess.h
         */
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
EXPORT_SYMBOL( write_digit_to_7seg );




static ssize_t gpio_lkm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
    unsigned int gpio, len = 0, i;
    char kbuf[BUF_SIZE];
    struct gpio_lkm_dev *gpio_lkm_devp = filp->private_data;


    len = count < BUF_SIZE ? count - 1 : BUF_SIZE - 1;

    if (raw_copy_from_user(kbuf, buf, len) != 0)
        return -EFAULT;

    kbuf[len] = '\0';
    if (len > 1) {
        for (i = 0; i < 7; ++i)
            gpio_set_value(symbols[i], low);
        printed_digit = -1;

        printk(KERN_INFO
        "[GPIO_LKM] - cannot set 7seg to %s\n", kbuf);
        return count;
    }
    if (write_digit_to_7seg(buf[0] - '0')) {
        printk("[7SEG] - Unable to write the specified digit %c", buf[0] - '0');
        return count;
    }

    printk(KERN_INFO
    "[GPIO_LKM] - Got request from user: %s\n", kbuf);




    printed_digit =  buf[0] - '0';

    *f_pos += count;
    return count;
}

/*
* gpio_lkm_init - Initialize GPIO device driver
* this function is called each time you call
* modprobe or insmod it should implement all the
* needed actions to prepare device to work
* here is a list:
* - dynamically register a character device major
* - create "GPIO" class in /sysfs
* - allocates resource for GPIO device
* - initialize the per-device data structure gpio_lkm_dev
* - register character device to the kernel
* - create device nodes to expose GPIO resource
*/
static int __init

gpio_lkm_init(void) {
    int i, ret, index = 0;

    /* register a range of char GPIO device numbers
     * here we request to allocate certain minor numbers
     * to correspondent GPIO devices pin numbers
     * more info in fs/char_dev.c:245
     */
    if (alloc_chrdev_region(&first, 0, 1, DEVICE_NAME) < 0) {
        printk(KERN_DEBUG
        "Cannot register device\n");
        return -1;
    }

    /* call create_class to create a udev class to contain the device
     * folder in also created in /sys/class for this module with name
     * defined in DEVICE_NAME
     */
    if ((gpio_lkm_class = class_create(THIS_MODULE, DEVICE_NAME)) == NULL) {
        printk(KERN_DEBUG
        "Cannot create class %s\n", DEVICE_NAME);
        /* clean up what was done in case of faulure
         */
        unregister_chrdev_region(first, 1);

        return -EINVAL;
    }

    gpio_lkm_devp = kmalloc(sizeof(struct gpio_lkm_dev), GFP_KERNEL);

    if (!gpio_lkm_devp)
    {
        printk(KERN_DEBUG
        "[GPIO_LKM]Bad kmalloc\n");
        return -ENOMEM;
    }

    for (index = 0; index < 7; ++index)
        if (gpio_request_one(symbols[index], GPIOF_OUT_INIT_LOW, NULL) < 0) {
            printk(KERN_ALERT
            "[GPIO_LKM] - Error requesting GPIO %d\n", i);
            return -ENODEV;
        }

    printed_digit = -1;
    gpio_lkm_devp->cdev.owner = THIS_MODULE;
    cdev_init(&gpio_lkm_devp->cdev, &gpio_lkm_fops);

    if ((ret = cdev_add(&gpio_lkm_devp->cdev, first, 1))) {
        printk(KERN_ALERT
        "[GPIO_LKM] - Error %d adding cdev\n", ret);


        device_destroy(gpio_lkm_class,
                       MKDEV(MAJOR(first),
                             MINOR(first)));


        /* clean up in opposite way from init
         */
        class_destroy(gpio_lkm_class);
        unregister_chrdev_region(first, 1);
        return ret;
    }

    if (device_create(gpio_lkm_class, NULL, MKDEV(MAJOR(first), MINOR(first)), NULL, "7seg") == NULL) {

        class_destroy(gpio_lkm_class);
        unregister_chrdev_region(first, 1);

        return -1;
    }


    printk("[GPIO_LKM] - Driver initialized\n");

    return 0;
}

/*
* gpio_lkm_exit - Deinitialize GPIO device driver when unloaded
* this function is called each time you call
* rmmod. it should implement all the oposite to
* initialization actions to deallocate resources
* used by device, unregister it from system
* here is a list:
* - release major number
* - release device nodes in /dev
* - release per-device structure arrays
* - detroy class in /sys
* - set all GPIO pins to output, low level
*/

static void __exit

gpio_lkm_exit(void) {
    int i = 0;

    unregister_chrdev_region(first, 1);
    kfree(gpio_lkm_devp);


    device_destroy(gpio_lkm_class, MKDEV(MAJOR(first), MINOR(first) + i));
    for (; i < 7; ++i)
        gpio_free(symbols[i]);
    /* destroy class
     */
    class_destroy(gpio_lkm_class);
    printk(KERN_INFO
    "[GPIO_LKM] - Raspberry Pi GPIO driver removed\n");
}

/* these are stantard macros to mark
 * init and exit functions implemetations
 */
module_init(gpio_lkm_init);
module_exit(gpio_lkm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roman Okhrimenko <mrromanjoe@gmail.com>");
MODULE_DESCRIPTION("GPIO Loadable Kernel Module - Linux device driver for Raspberry Pi");

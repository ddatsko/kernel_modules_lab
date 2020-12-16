#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>

extern int read_digit_from_7seg(void);
extern int write_digit_to_7seg(int digit);


static int seven_seg_digit;


static ssize_t seven_seg_digit_show(struct kobject *kobj, struct kobj_attribute *attr,
                        char *buf)
{
    return sprintf(buf, "%d\n", read_digit_from_7seg());
}

static ssize_t seven_seg_digit_store(struct kobject *kobj, struct kobj_attribute *attr,
                         const char *buf, size_t count)
{
    int ret;
    int digit;

    ret = kstrtoint(buf, 10, &digit);
    if (ret < 0) {
        printk(KERN_WARNING "Failed to convert string to number");
        return ret;
    }
    if (write_digit_to_7seg(digit)) {
        printk(KERN_ERR "[K7seg] - Error while displaying the digit %d", digit);
        return -1;
    }

    return count;
}

static struct kobj_attribute seven_seg_digit_attribute =
        __ATTR(7seg, 0664, seven_seg_digit_show, seven_seg_digit_store);



static struct attribute *attrs[] = {
        &seven_seg_digit_attribute.attr,
        NULL,
};


static struct attribute_group attr_group = {
        .attrs = attrs,
};

static struct kobject *seven_seg_kobj;

static int __init example_init(void)
{
    int retval;

    seven_seg_kobj = kobject_create_and_add("7seg", kernel_kobj);
    if (!seven_seg_kobj)
        return -ENOMEM;

    retval = sysfs_create_group(seven_seg_kobj, &attr_group);
    if (retval)
        kobject_put(seven_seg_kobj);

    return retval;
}

static void __exit example_exit(void)
{
    kobject_put(seven_seg_kobj);
}

module_init(example_init);
module_exit(example_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Denys Datsko, Nazar Pasternak, Sofia Petryshyn");
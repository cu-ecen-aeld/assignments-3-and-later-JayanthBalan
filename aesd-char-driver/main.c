
/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include "aesdchar.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Jayanth Balan"); /** fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * handle open
     */
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t entry_offset;
    size_t bytes_to_copy;
    size_t current_pos;
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = filp->private_data;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * handle read
     */
    if(count == 0) {
        return 0;
    }

    mutex_lock(&dev->lock);

    current_pos = (size_t)*f_pos;

    while(retval < (ssize_t)count) {
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, current_pos, &entry_offset);
        if(entry == NULL) {
            break;
        }

        bytes_to_copy = entry->size - entry_offset;
        if(bytes_to_copy > count - retval) {
            bytes_to_copy = count - retval;
        }

        if(copy_to_user(buf + retval, entry->buffptr + entry_offset, bytes_to_copy)) {
            mutex_unlock(&dev->lock);
            return retval > 0 ? retval : -EFAULT;
        }

        retval += bytes_to_copy;
        current_pos += bytes_to_copy;
    }

    *f_pos = current_pos;

    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    size_t total_size;
    size_t command_start;
    size_t command_size;
    size_t pending_size;
    size_t command_count = 0;
    size_t command_index = 0;
    const char *newline;
    char *new_buffer;
    char *pending_buffer = NULL;
    char *entry_buffer;
    struct aesd_buffer_entry *new_entries = NULL;
    struct aesd_dev *dev = filp->private_data;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    /**
     * handle write
     */
    if(count == 0) {
        return 0;
    }

    mutex_lock(&dev->lock);

    pending_size = dev->working_entry.size;
    total_size = pending_size + count;

    new_buffer = kmalloc(total_size, GFP_KERNEL);
    if(!new_buffer) {
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }

    if(pending_size > 0) {
        memcpy(new_buffer, dev->working_entry.buffptr, pending_size);
    }

    if(copy_from_user(new_buffer + pending_size, buf, count)) {
        kfree(new_buffer);
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }

    command_start = 0;

    while(command_start < total_size) {
        newline = memchr(new_buffer + command_start, '\n', total_size - command_start);
        if(!newline) {
            break;
        }

        command_count++;
        command_size = newline - (new_buffer + command_start) + 1;
        command_start += command_size;
    }

    if(command_count > 0) {
        new_entries = kmalloc_array(command_count, sizeof(*new_entries), GFP_KERNEL);
        if(!new_entries) {
            kfree(new_buffer);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
    }

    command_start = 0;

    while(command_index < command_count) {
        newline = memchr(new_buffer + command_start, '\n', total_size - command_start);
        command_size = newline - (new_buffer + command_start) + 1;
        entry_buffer = kmalloc(command_size, GFP_KERNEL);
        if(!entry_buffer) {
            while(command_index > 0) {
                command_index--;
                kfree(new_entries[command_index].buffptr);
            }
            kfree(new_entries);
            kfree(new_buffer);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }

        new_entries[command_index].buffptr = entry_buffer;
        new_entries[command_index].size = command_size;
        memcpy(entry_buffer, new_buffer + command_start, command_size);
        command_start += command_size;
        command_index++;
    }

    if(command_start < total_size) {
        pending_buffer = kmalloc(total_size - command_start, GFP_KERNEL);
        if(!pending_buffer) {
            while(command_index > 0) {
                command_index--;
                kfree(new_entries[command_index].buffptr);
            }
            kfree(new_entries);
            kfree(new_buffer);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }

        memcpy(pending_buffer, new_buffer + command_start, total_size - command_start);
    }

    kfree(dev->working_entry.buffptr);
    dev->working_entry.buffptr = pending_buffer;
    dev->working_entry.size = total_size - command_start;

    command_index = 0;

    while(command_index < command_count) {
        if(dev->buffer.full) {
            kfree(dev->buffer.entry[dev->buffer.out_offs].buffptr);
        }

        aesd_circular_buffer_add_entry(&dev->buffer, &new_entries[command_index]);
        command_index++;
    }

    kfree(new_entries);
    kfree(new_buffer);
    retval = count;

    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);
    aesd_device.working_entry.buffptr = NULL;
    aesd_device.working_entry.size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if(result) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t index;
    struct aesd_buffer_entry *entry;

    cdev_del(&aesd_device.cdev);

    /**
     * cleanup AESD specific poritions here as necessary
     */
    kfree(aesd_device.working_entry.buffptr);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        kfree(entry->buffptr);
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

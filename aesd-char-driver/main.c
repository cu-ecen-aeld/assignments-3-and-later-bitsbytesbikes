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
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Robert Eichinger");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    size_t bytes_deliverable;
    unsigned long bytes_remaining;


    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if(mutex_lock_interruptible(&aesd_device.mutex))
    {
        return -ERESTARTSYS;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
        &aesd_device.circ_buf,
        *f_pos, 
        &entry_offset);
    if(!entry)
    {
        PDEBUG("Entry could not be found in circular buffer");
        mutex_unlock(&aesd_device.mutex);
        return -EFAULT;
    }

    bytes_deliverable = count;
    if(bytes_deliverable > (entry->size - entry_offset))
    {
        bytes_deliverable = entry->size - entry_offset;
    }

    bytes_remaining = copy_to_user(buf, &entry->buffptr[entry_offset], bytes_deliverable);
    
    if(bytes_remaining > 0)
    {
        PDEBUG("Not all bytes could be copied to the requested section");
        mutex_unlock(&aesd_device.mutex);
        return -ERESTARTSYS;
    }

    retval = bytes_deliverable;
    *f_pos += bytes_deliverable;
    
    mutex_unlock(&aesd_device.mutex);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    size_t  uncopied = 0;
    unsigned int newline_index;
    bool newline_found = false;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    if(mutex_lock_interruptible(&aesd_device.mutex))
    {
        return -ERESTARTSYS;
    }

    if(aesd_device.command_buffer == NULL)
    {
        PDEBUG("Initializing the command buffer with 100 bytes");
        aesd_device.command_buffer = kmalloc(100, GFP_KERNEL); 
        aesd_device.command_buffer_size = 100;
    }

    if((count + aesd_device.bytes_in_command_buffer) > aesd_device.command_buffer_size)
    {
        PDEBUG("Command buffer size will be exceeded, doubling the memory...");
        aesd_device.command_buffer_size *= 2;
        aesd_device.command_buffer = krealloc(aesd_device.command_buffer, aesd_device.command_buffer_size, GFP_KERNEL);
    }
    
    if(aesd_device.command_buffer == NULL)
    {
        mutex_unlock(&aesd_device.mutex);
        PDEBUG("Allocation failed");
        return -ENOMEM;
    }

    PDEBUG("Writing to command buffer index %ld", aesd_device.bytes_in_command_buffer);
    uncopied = copy_from_user(&aesd_device.command_buffer[aesd_device.bytes_in_command_buffer], buf, count);
    if(uncopied != 0)
    {
        PDEBUG("Not all bytes could be copied from userspace");
    }

    aesd_device.bytes_in_command_buffer += (count - uncopied);
    retval = (count - uncopied);

    for(newline_index = 0; newline_index < aesd_device.bytes_in_command_buffer; newline_index++)
    {
        if(aesd_device.command_buffer[newline_index] == '\n')
        {
            newline_found = true;
            break;
        }
    }

    if(newline_found)
    {
        newline_index++;
        char *command_buf = kmalloc(newline_index, GFP_KERNEL);
        struct aesd_buffer_entry entry;
        int i = 0;
        
        if(command_buf == NULL)
        {
            PDEBUG("command buffer could not be allocated");
            mutex_unlock(&aesd_device.mutex);
            return -ENOMEM;
        }
        memcpy(command_buf, aesd_device.command_buffer, newline_index);
        // free the old entry in the ringbuffer
        if(aesd_device.circ_buf.full)
        {
            // This should not be done here
            kfree(aesd_device.circ_buf.entry[aesd_device.circ_buf.in_offs].buffptr);
        }
        entry.buffptr = command_buf;
        entry.size = newline_index;
        aesd_circular_buffer_add_entry(&aesd_device.circ_buf, &entry);
        
        aesd_device.bytes_in_command_buffer -= newline_index;
        while(newline_index < aesd_device.bytes_in_command_buffer)        
        {
            aesd_device.command_buffer[i] = aesd_device.command_buffer[newline_index];
            i++;
        }
    }

    mutex_unlock(&aesd_device.mutex);

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
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.mutex);
    aesd_device.command_buffer = NULL;
    aesd_device.command_buffer_size = 0;
    aesd_device.bytes_in_command_buffer = 0;
    aesd_circular_buffer_init(&aesd_device.circ_buf);

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

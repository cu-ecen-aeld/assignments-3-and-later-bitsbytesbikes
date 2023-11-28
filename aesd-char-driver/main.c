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
    uint8_t out_index = aesd_device.circ_buf.out_offs;
    uint8_t entry_count = 0;
    size_t bytes_remaining = count;
    size_t bytes_skipped = 0;
    size_t bytes_copied = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if(mutex_lock_interruptible(&aesd_device.mutex))
    {
        return -ERESTARTSYS;
    }

    while((aesd_device.circ_buf.entry[out_index].buffptr != NULL) 
            && (entry_count < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED))
    {
        size_t bytes_to_copy = aesd_device.circ_buf.entry[out_index].size;
        size_t bytes_not_copied;

        if(bytes_remaining < bytes_to_copy)
        {
            bytes_to_copy = bytes_remaining;
        }
        
        if(bytes_skipped < *f_pos)
        {
            bytes_skipped += aesd_device.circ_buf.entry[out_index].size;
        }
        else
        {
            // for debugging only
            #if 0
                char buffer[100] = {0};
                memcpy(buffer, aesd_device.circ_buf.entry[out_index].buffptr, aesd_device.circ_buf.entry[out_index].size);
                PDEBUG("Processing entry %s", buffer);
            #endif

            bytes_not_copied = copy_to_user(&buf[bytes_copied], aesd_device.circ_buf.entry[out_index].buffptr, bytes_to_copy);

            if(bytes_not_copied)
            {
                mutex_unlock(&aesd_device.mutex);
                return -ERESTARTSYS;
            }

            bytes_remaining -= bytes_to_copy;
            bytes_copied += bytes_to_copy;

            if(bytes_remaining == 0)
            {
                break;
            }
        }

        out_index++;
        out_index %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entry_count++;
    }

    retval = bytes_copied;
    *f_pos += bytes_copied;
    
    mutex_unlock(&aesd_device.mutex);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    size_t  uncopied = 0;
    unsigned int last_newline_index;
    unsigned int i;
    unsigned int command_start_index = 0;
    bool newline_in_buffer = false;

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

    // for debugging only
    #if 0
        char tmp_buf[100] = {0};
        memcpy(tmp_buf, buf, count);
        PDEBUG("Receiving buffer from user %s", tmp_buf);
        PDEBUG("Number of bytes in buffer before copying: %lu", aesd_device.bytes_in_command_buffer);
    #endif

    uncopied = copy_from_user(&aesd_device.command_buffer[aesd_device.bytes_in_command_buffer], buf, count);
    if(uncopied != 0)
    {
        PDEBUG("Not all bytes could be copied from userspace");
    }

    aesd_device.bytes_in_command_buffer += (count - uncopied);
    retval = (count - uncopied);

    for(i= 0; i < aesd_device.bytes_in_command_buffer;i++)
    {
        if(aesd_device.command_buffer[i] == '\n')
        {
            size_t command_len = i + 1 - command_start_index;
            char *command_buf = kmalloc(command_len, GFP_KERNEL);
            struct aesd_buffer_entry entry;
            last_newline_index = i;
            newline_in_buffer = true;
            if(command_buf == NULL)
            {
                PDEBUG("Allocation of command buffer failed");
                mutex_unlock(&aesd_device.mutex);
                return -ENOMEM;
            }
            memcpy(command_buf, &aesd_device.command_buffer[command_start_index], command_len);
            // for debugging only
            #if 0
                char buffer[100] = {0};
                memcpy(buffer, command_buf, command_len);
                PDEBUG("Processed command: %s", buffer);
            #endif
            // free the old entry in the ringbuffer
            if(aesd_device.circ_buf.full)
            {
                // This should not be done here
                kfree(aesd_device.circ_buf.entry[aesd_device.circ_buf.in_offs].buffptr);
            }
            entry.buffptr = command_buf;
            entry.size = command_len;
            aesd_circular_buffer_add_entry(&aesd_device.circ_buf, &entry);
            command_start_index = i + 1;
        }
    }

    if(newline_in_buffer)
    {
        int idx = 0;
        last_newline_index++;
        while(last_newline_index < aesd_device.bytes_in_command_buffer)
        {
            aesd_device.command_buffer[idx] = aesd_device.command_buffer[last_newline_index++];
            idx++;
        }
        aesd_device.bytes_in_command_buffer = idx;
    }

    // for debugging only
    #if 0
    int index = 0;
    struct aesd_buffer_entry *e;
    PDEBUG("DEBUG BUFFER");
    AESD_CIRCULAR_BUFFER_FOREACH(e, &aesd_device.circ_buf, index)
    {
        if(e->buffptr !=NULL)
        {
            char buffer[100] = {0};
            memcpy(buffer, e->buffptr, e->size);
            PDEBUG("entry: %s", buffer);
        }
    }
    #endif

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

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    int index = 0;
    struct aesd_buffer_entry *e;

    cdev_del(&aesd_device.cdev);

    PDEBUG("DEBUG BUFFER");
    AESD_CIRCULAR_BUFFER_FOREACH(e, &aesd_device.circ_buf, index)
    {
        if(e->buffptr !=NULL)
        {
            kfree(e->buffptr);
        }
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

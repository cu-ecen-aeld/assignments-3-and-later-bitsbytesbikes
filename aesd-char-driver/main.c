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
#include "aesdchar.h"
#include "aesd_ioctl.h"

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
        
        if((bytes_skipped + aesd_device.circ_buf.entry[out_index].size) <= *f_pos)
        {
            bytes_skipped += aesd_device.circ_buf.entry[out_index].size;
        }
        else
        {
            int idx = *f_pos - bytes_skipped;

            if(bytes_to_copy > idx)
                bytes_to_copy -= idx;
            
            PDEBUG("Copying %lu bytes from idex :%d",bytes_to_copy,idx);
            // for debugging only
            #if 0
                char buffer[100] = {0};
                memcpy(buffer, aesd_device.circ_buf.entry[out_index].buffptr, aesd_device.circ_buf.entry[out_index].size);
                PDEBUG("Processing entry %s", buffer);
                PDEBUG("bytes_copied: %lu - f_pos: %lli - bytes_skipped: %lu - aesd.fsize: %lu - idx: %d, bytes_to_copy: %lu", 
                    bytes_copied, *f_pos, bytes_skipped, aesd_device.fsize, idx, bytes_to_copy);
            #endif

            bytes_not_copied = copy_to_user(&buf[bytes_copied], &aesd_device.circ_buf.entry[out_index].buffptr[idx], bytes_to_copy);

            if(bytes_not_copied)
            {
                mutex_unlock(&aesd_device.mutex);
                return -ERESTARTSYS;
            }

            bytes_remaining -= bytes_to_copy;
            bytes_copied += bytes_to_copy;
            break;
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


static void aesd_update_fsize(void)
{
    int index = 0;
    struct aesd_buffer_entry *e;
    size_t fsize = 0;
    AESD_CIRCULAR_BUFFER_FOREACH(e, &aesd_device.circ_buf, index)
    {
        if(e->buffptr !=NULL)
        {
            fsize += e->size;
        }
    }
    aesd_device.fsize = fsize;
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
    aesd_update_fsize();

    mutex_unlock(&aesd_device.mutex);

    return retval;
}

loff_t aesd_llseek (struct file *filp, loff_t off, int whence)
{
	long newpos;

    PDEBUG("aesd_llseek off:%llu ", off);
    newpos = fixed_size_llseek(filp, off, whence, aesd_device.fsize);

	return newpos;
}

static int seek_to_command(struct file *filp, uint32_t cmd, uint32_t off)
{
    int ret = -EINVAL;
    uint8_t out_index = aesd_device.circ_buf.out_offs;
    uint8_t entry_count = 0;
    uint32_t file_offset = filp->f_pos;
    
    if(mutex_lock_interruptible(&aesd_device.mutex))
    {
        return -ERESTARTSYS;
    }

    while((aesd_device.circ_buf.entry[out_index].buffptr != NULL) 
            && (entry_count < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED))
    {
        if(entry_count == cmd)
        {
            PDEBUG("Reached entry %u", entry_count);
            PDEBUG("Size of entry %lu",aesd_device.circ_buf.entry[out_index].size );
            if(off < aesd_device.circ_buf.entry[out_index].size)
            {
                file_offset += off;
                mutex_unlock(&aesd_device.mutex);
                loff_t retval = aesd_llseek (filp, file_offset, SEEK_SET);
                filp->f_pos = retval;
                PDEBUG("seeked to file_offset %llu", retval);
                return retval;
            }   
            break;         
        }        
        file_offset += aesd_device.circ_buf.entry[out_index].size;
        out_index++;
        out_index %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entry_count++;
    }

    mutex_unlock(&aesd_device.mutex);

    return ret;
}

long aesd_ioctl (struct file *filp,
                 unsigned int cmd, unsigned long arg)
{
	int ret = 0;
    struct aesd_seekto seek_command;
    
    PDEBUG("aesd_ioctl (command=%u, arg=%lu)", cmd, arg);
 
	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

	switch(cmd) {

        case AESDCHAR_IOCSEEKTO:
        {
            if(copy_from_user(&seek_command, (void *)arg, sizeof(struct aesd_seekto)) != 0)
            {
                return -ERESTARTSYS;
            }
            PDEBUG("AESDCHAR_IOCSEEKTO ioctl with cmd: %u and offset %u", seek_command.write_cmd, seek_command.write_cmd_offset);
            ret = seek_to_command(filp, seek_command.write_cmd, seek_command.write_cmd_offset);
            break;
        }

        default:
        {
            return -ENOTTY;
        }
	}

	return ret;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .llseek =   aesd_llseek,
    .unlocked_ioctl =    aesd_ioctl,
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

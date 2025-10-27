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
#include <linux/mutex.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Harshal Wadhwa"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */

    struct aesd_dev* dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    
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
    size_t entry_offset_byte;
    size_t max_bytes_to_read = 0;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

    if (mutex_lock_interruptible(&dev->buffer_lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &entry_offset_byte);

    if(entry == NULL)
    {
        retval = 0;
        mutex_unlock(&dev->buffer_lock);
        return retval;
    }

    max_bytes_to_read = entry->size - entry_offset_byte;

    if(max_bytes_to_read > count)
    {
        max_bytes_to_read = count;
    }

    if(copy_to_user(buf, entry->buffptr + entry_offset_byte, max_bytes_to_read))
    {
        retval = -EFAULT;
        mutex_unlock(&dev->buffer_lock);
        return retval;
    }

    retval = max_bytes_to_read;
    *f_pos += max_bytes_to_read;
    mutex_unlock(&dev->buffer_lock);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

    struct aesd_dev *dev = filp->private_data;
    char* buffer;
    int data_complete = 0;

    if (mutex_lock_interruptible(&dev->buffer_lock))
        return -ERESTARTSYS;

    buffer = kmalloc(count, GFP_KERNEL);
    if(buffer == NULL)
    {
        retval = -ENOMEM;
        mutex_unlock(&dev->buffer_lock);
        return retval;
    }

    if(copy_from_user(buffer, buf, count))
    {
        retval = -EFAULT;
        kfree(buffer);
        mutex_unlock(&dev->buffer_lock);
        return retval;
    }

    for(int i = 0; i < count; ++i)
    {
        if (buffer[i] == '\n')
        {
            data_complete = 1;
            break;
        }
    }

    char* realloc_buffer = krealloc(dev->write_buffer_ptr, dev->write_buffer_size + count, GFP_KERNEL);

    if(realloc_buffer == NULL)
    {
        retval = -ENOMEM;
        kfree(buffer);
        mutex_unlock(&dev->buffer_lock);
        return retval;   
    }
    
    dev->write_buffer_ptr = realloc_buffer; 
    memcpy(dev->write_buffer_ptr + dev->write_buffer_size, buffer, count);
    dev->write_buffer_size += count;
    
    if(data_complete)
    {
        struct aesd_buffer_entry latest_node;
        char* free_buffer = NULL;

        if(dev->circular_buffer.full)
        {
            free_buffer = (char*) dev->circular_buffer.entry[dev->circular_buffer.in_offs].buffptr;
        }

        latest_node.buffptr = dev->write_buffer_ptr;
        latest_node.size = dev->write_buffer_size;

        aesd_circular_buffer_add_entry(&dev->circular_buffer, &latest_node);

        if(free_buffer != NULL)
        {
            kfree(free_buffer);
        }

        dev->write_buffer_ptr = NULL;
        dev->write_buffer_size = 0;
   }
   
   retval = count;
   
    kfree(buffer);
    mutex_unlock(&dev->buffer_lock);

    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;
    uint8_t i = dev->circular_buffer.out_offs;
    size_t curr_total_size = 0;

    if (mutex_lock_interruptible(&dev->buffer_lock))
        return -ERESTARTSYS;

    do
    {
        curr_total_size += dev->circular_buffer.entry[i].size;
        i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
   } while(i != dev->circular_buffer.in_offs);

   switch(whence)
   {
      case 0: /* SEEK_SET */
        newpos = off;
        break;

      case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

      case 2: /* SEEK_END */
        newpos = curr_total_size + off;
        break;

      default: /* can't happen */
        mutex_unlock(&dev->buffer_lock);
        return -EINVAL;
    }

    if (newpos < 0)
    {
        mutex_unlock(&dev->buffer_lock);
        return -EINVAL;
    }

    filp->f_pos = newpos;
    mutex_unlock(&dev->buffer_lock);
    return newpos;
    
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    size_t no_of_commands = 0;
    int buffer_index = 0;
    size_t curr_total_size = 0;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    switch(cmd)
    {
        case AESDCHAR_IOCSEEKTO:
        if( copy_from_user(&seekto, (const void __user *) arg, 8))
        {
            return -EFAULT;
        }

        if (mutex_lock_interruptible(&dev->buffer_lock))
        return -ERESTARTSYS;

        if (dev->circular_buffer.in_offs > dev->circular_buffer.out_offs)
        {
            no_of_commands = dev->circular_buffer.in_offs - dev->circular_buffer.out_offs;
        }
        else
        {
            no_of_commands = (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - dev->circular_buffer.out_offs) + dev->circular_buffer.in_offs;
            if(no_of_commands == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED && !dev->circular_buffer.full) no_of_commands = 0;
        }

        if (seekto.write_cmd >= no_of_commands)
        {
            mutex_unlock(&dev->buffer_lock);
            return -EINVAL;
        }

        buffer_index = (dev->circular_buffer.out_offs + seekto.write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        if (seekto.write_cmd_offset >= dev->circular_buffer.entry[buffer_index].size)
        {
            mutex_unlock(&dev->buffer_lock);
            return -EINVAL;
        }

        uint8_t i = 0;
        while(i < seekto.write_cmd)
        {
            curr_total_size += dev->circular_buffer.entry[ (i + dev->circular_buffer.out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ].size;
            ++i;
        }

        curr_total_size += seekto.write_cmd_offset;
        filp->f_pos = curr_total_size;

        mutex_unlock(&dev->buffer_lock);
        return 0;

        default:
            return -ENOTTY;
    }

}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    mutex_init(&aesd_device.buffer_lock);
    aesd_circular_buffer_init(&aesd_device.circular_buffer);

    aesd_device.write_buffer_ptr = NULL;
    aesd_device.write_buffer_size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result )
    {
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

    mutex_lock(&aesd_device.buffer_lock);

    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
    {
        if (aesd_device.circular_buffer.entry[i].buffptr != NULL)
        {
            kfree(aesd_device.circular_buffer.entry[i].buffptr);
            aesd_device.circular_buffer.entry[i].buffptr = NULL;
        }
    }

    if (aesd_device.write_buffer_ptr != NULL)
    {
        kfree(aesd_device.write_buffer_ptr);
        aesd_device.write_buffer_ptr = NULL;
    }

    mutex_unlock(&aesd_device.buffer_lock);
    mutex_destroy(&aesd_device.buffer_lock);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);



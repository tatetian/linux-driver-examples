#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

MODULE_LICENSE("Dual BSD/GPL");

dev_t dev_id;
unsigned int scull2_major, scull2_minor = 0, scull2_ndevs = 1;
int qset_size = 1024;
int quantum_bytes = 4096;
int scull2_devices_num = 4;
struct scull2_qset {
  void **data;
  struct scull2_qset *next;
};

struct scull2_dev {
  struct scull2_qset *qset;
  unsigned long size;
  unsigned int access_key;
  struct semaphore sem;
  struct cdev cdev;
};

struct scull2_dev *scull2_device;

int read_proc_scull2(char *buf, char **start, off_t offset, 
                        int count, int *eof, void *data)
{
    int i, len = 0;
    int limit = count - 80;
    struct scull2_dev *sd = scull2_device;
    struct scull2_qset *qs = sd->qset;
    printk("start of read_proc_scull2\n");
    if (down_interruptible(&sd->sem))
    {
       return -ERESTARTSYS;
    }
    for(; qs && len <= limit; qs = qs->next)
    {
       len += sprintf(buf + len, "item at %p, qset at %p\n", qs, qs->next);
       printk("===========================================\n");
       if (qs->data && !qs->next) 
         for (i = 0; i < qset_size; i++) {
           if (qs->data[i])
           {
              len += sprintf(buf + len," %4i: %8p\n", i, qs->data[i]);
           }
         }  
    }
     up(&sd->sem);
    *eof = 1;
   printk("end of\n");
   return len;
 }
//////==============   
 struct scull2_qset* scull2_get_qset(struct scull2_dev *dev, int index) {
    int i = 0;
    struct scull2_qset* qset = dev->qset;

  if (!qset) {
    qset = dev->qset = kmalloc(sizeof(struct scull2_qset), GFP_KERNEL);
    if (qset == NULL)
      return NULL;  /* Never mind */
    memset(qset, 0, sizeof(struct scull2_qset));
  }

  while(qset && i < index) {
    qset = qset->next; 
    i ++; 
  }
  return qset;
} 

int scull2_trim(struct scull2_dev *dev) {
  struct scull2_qset *next_q, *curr_q;
  int i;

  for(curr_q = dev->qset; curr_q; curr_q = next_q) {
    if (curr_q->data) {
      for  (i = 0; i < qset_size; i++)
        kfree(curr_q->data[i]); 
      kfree(curr_q->data);
      curr_q->data = NULL; 
    } 
    next_q = curr_q->next;
    kfree(curr_q);
  }
  dev->size = 0;
  dev->qset = NULL;
  return 0;
}

/* ===========================================================================
 *  Scull2 file operations 
 * ======================================================================== */
int scull2_open(struct inode *inode, struct file *filp) {
  struct scull2_dev *dev;

  dev = container_of(inode->i_cdev, struct scull2_dev, cdev);
  filp->private_data = dev;

  if ( (filp->f_flags & O_ACCMODE) == O_WRONLY ) {
    scull2_trim(dev);
  }
  return 0;
}

ssize_t scull2_read(struct file *filp, char __user *buf, size_t count,
                    loff_t *f_pos)
{
  struct scull2_dev *dev = filp->private_data;
  struct scull2_qset *qset;
  int qset_bytes = qset_size * quantum_bytes;
  int qset_index, qset_rest, quantum_index, quantum_rest, quantum_remain;
  ssize_t retval = 0;

  if (down_interruptible(&dev->sem))
    return -ERESTARTSYS;
  if (*f_pos >= dev->size)
    goto out;
  if (*f_pos + count > dev->size)
    count = dev->size - (long)*f_pos;

  qset_index = (long)*f_pos / qset_bytes;
  qset_rest  = (long)*f_pos % qset_bytes;
  quantum_index = qset_rest / quantum_bytes;
  quantum_rest  = qset_rest % quantum_bytes;

  qset = scull2_get_qset(dev, qset_index);

  if (!qset || !qset->data || !qset->data[quantum_index])
    goto out;

  /* read only up to the end of this quantum */
  quantum_remain = quantum_bytes - quantum_rest;
  if (count > quantum_remain)
    count = quantum_remain;

  if (copy_to_user(buf, qset->data[quantum_index] + quantum_rest, count)) {
    retval = -EFAULT;
    goto out;
  }
  *f_pos += count;
  retval = count;
out:
  up(&dev->sem);
  return retval;
}

ssize_t scull2_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos) {
  struct scull2_dev *dev = filp->private_data;
  struct scull2_qset *qset;
  int qset_bytes = qset_size * quantum_bytes;
  int qset_index, qset_rest, quantum_index, quantum_rest, quantum_remain;
  ssize_t retval = -ENOMEM;

  if (down_interruptible(&dev->sem))
    return -ERESTARTSYS;

  qset_index = (long)*f_pos / qset_bytes;
  qset_rest  = (long)*f_pos % qset_bytes;
  quantum_index = qset_rest / quantum_bytes;
  quantum_rest  = qset_rest % quantum_bytes;

  qset = scull2_get_qset(dev, qset_index);
  if (!qset)
    goto out;
  if (!qset->data) {
    qset->data = kmalloc(qset_size * sizeof(char*), GFP_KERNEL);
    if (!qset->data)
      goto out;
    memset(qset->data, 0, qset_size * sizeof(char*)); 
  }
  if (!qset->data[quantum_index]) {
    qset->data[quantum_index] = kmalloc(quantum_bytes, GFP_KERNEL);
    if (!qset->data[quantum_index])
      goto out;
  }

  /* write only up to the end of this quantum */
  quantum_remain = quantum_bytes - quantum_rest;
  if (count > quantum_remain)
    count = quantum_remain; 

  if (copy_from_user(qset->data[quantum_index] + quantum_rest, buf, count)) {
    retval = -EFAULT;
    goto out;
  }
  *f_pos += count;
  retval = count;

  if (dev->size < *f_pos)
    dev->size = *f_pos;

out:
  up(&dev->sem);
  return retval;
}

loff_t scull2_llseek(struct file *filp, loff_t off, int whence)
{
  struct scull2_dev *dev = filp->private_data;
  loff_t newpos;

  switch(whence) {
  case 0: /* SEEK_SET */
    newpos = off;
    break;

  case 1: /* SEEK_CUR */
    newpos = filp->f_pos + off;
    break;

  case 2: /* SEEK_END */
    newpos = dev->size + off;
    break;

  default: /* can't happen */
    return -EINVAL;
  }
  if (newpos < 0) return -EINVAL;
  filp->f_pos = newpos;
  return newpos;
}


int scull2_release(struct inode *inode, struct file* filp) {
  return 0;
}

struct file_operations scull2_fops = {
  .owner    =   THIS_MODULE,
  .llseek   =   scull2_llseek,
  .read     =   scull2_read,
  .write    =   scull2_write,
 // .ioctl    =   scull2_ioctl,
  .open     =   scull2_open,
  .release  =   scull2_release,
};

/* ===========================================================================
 *  Scull2 functions 
 * ======================================================================== */

static void scull2_setup_cdev(struct scull2_dev *dev) {
  int err;

  cdev_init(&dev->cdev, &scull2_fops);
  dev->cdev.owner = THIS_MODULE;

  err = cdev_add(&dev->cdev, dev_id, 1);
  if(err)
    printk(KERN_NOTICE "Error %d: adding scull2 failed", err);
}

static int scull2_init(void)
{
  int res;
  struct scull2_dev *dev;

  res = alloc_chrdev_region(&dev_id, scull2_minor, scull2_ndevs, "scull2");
  if (res < 0) {
    printk(KERN_WARNING "scull2: failed to get a valid device id");
    return res; 
  } 
   
  dev = kmalloc(sizeof(struct scull2_dev), GFP_KERNEL);
  if (!dev) {
    printk(KERN_WARNING "scull2: failed to allocate memory");
    return -1;
  }
  memset(dev, 0, sizeof(*dev));
  dev->qset = NULL;
  dev->size = 0;
  sema_init(&dev->sem, 1);
  scull2_setup_cdev(dev);

  scull2_device = dev;

  create_proc_read_entry("scull2", 0, NULL, read_proc_scull2, NULL);

  printk(KERN_ALERT "Scull2 init successfully\n");
  return 0;
}

static void scull2_exit(void)
{
  unregister_chrdev_region(dev_id, scull2_ndevs);
  remove_proc_entry("scull2", NULL);
  printk(KERN_ALERT "scull2 exit successfully\n");
}

module_init(scull2_init);
module_exit(scull2_exit);

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("Dual BSD/GPL");

dev_t dev_id;
unsigned int scull2_major, scull2_minor = 0, scull2_ndevs = 1;
int qset_size = 1024;
int quantum_size = 4096;

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

int scull2_trim(struct scull2_dev *dev) {
  struct scull_qset *next_q, *curr_q;
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

int scull2_release(struct inode *inode, struct file* filp) {
  return 0;
}

struct file_operations scull2_fops = {
  .owner    =   THIS_MODULE,
  .llseek   =   scull2_llseek,
  .read     =   scull2_read,
  .write    =   scull2_write,
  .ioctl    =   scull2_ioctl,
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
  
  printk(KERN_ALERT "Scull2 init\n");
  
  res = alloc_chrdev_region(&dev_id, scull2_minor, scull2_ndevs, "scull2");
  if (res < 0) {
    printk(KERN_WARNING "scull2: failed to get a valid device id");
    return res; 
  } 
   

  return 0;
}

static void scull2_exit(void)
{
  printk(KERN_ALERT "scull2 exit\n");
}

module_init(scull2_init);
module_exit(scull2_exit);

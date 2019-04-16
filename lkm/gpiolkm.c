#include <linux/init.h>             
#include <linux/module.h>    
#include <linux/device.h>         
#include <linux/kernel.h>    
#include <linux/fs.h> 
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
 
MODULE_LICENSE("GPL");              
MODULE_AUTHOR("Chris Morrison");      
MODULE_DESCRIPTION("A simple Linux driver to record GPIO interrupts");  
MODULE_VERSION("0.1");  

typedef struct 
{
   int gpioNum;
   int irqNum;
   char gpioName[8];
   struct timespec tsLast;
   struct timespec tsCurrent;
   struct timespec tsDiff;
   int intCount;

   struct cdev gpiocdev;

   struct attribute_group attrGroup;
} gpioDevPriv;



enum {NUM_GPIO = 256};

static gpioDevPriv gpioDevice[NUM_GPIO];

static const char DEVICE_NAME[] = "gpio_input_driver";
static const char CLASS_NAME[] = "gpio_inputs";

static dev_t devNum;
static struct cdev drivercdev;
 
static void addHandler(int gpio);
static void removeHandler(int gpio);
static int devOpen(struct inode *, struct file *); 
static int gpioOpen(struct inode *, struct file *); 
static ssize_t devWrite(struct file *filep, const char *buffer, size_t len, loff_t *offset);
static irq_handler_t gpioIRQ_Handler(unsigned int irq, void * dev_id, struct pt_regs * regs);

static struct attribute *gpio_class_attrs[] = 
{
  NULL
};
ATTRIBUTE_GROUPS(gpio_class);

static struct class gpioClass = 
{
  .name = "gpio_inputs",
  .owner = THIS_MODULE,
  .class_groups = gpio_class_groups
};

/* file ops supported by the top level driver */
static struct file_operations driver_fops =
{
    .open = devOpen,
    .write = devWrite
};

/* file ops supported by each gpio device */
static struct file_operations gpio_fops =
{
    .open = gpioOpen
};

static int gpioOpen(struct inode *inodep, struct file *filep)
{
   return 0;
}

static int devOpen(struct inode *inodep, struct file *filep)
{
   return 0;
}

static ssize_t devWrite(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
   char buf[10] = {0};
   long gpio = 0;

   if ((len < 2) || len > (sizeof(buf)- 1))
   {
      return -EINVAL;
   }
   
   copy_from_user(buf, buffer, len);

   printk(KERN_INFO "gpio-irq: string from userspace: %s\n", buf);

   if (0 == kstrtol(buf, 10, &gpio))
   {
      if (gpio > 0)
      {
         printk(KERN_INFO "gpio-irq: Adding handler to gpio %ld\n", gpio);
         addHandler(gpio);
      }
      else
      {
         gpio *= -1;
         printk(KERN_INFO "gpio-irq: Removing handler from gpio %ld\n", gpio);
         removeHandler(gpio);
      }
   }
   else
   {
      return -EINVAL;
   }
   return len;
}

static ssize_t intCount_show(struct device * dev, struct device_attribute *attr, char *buf)
{
   ssize_t ret = 0;
   gpioDevPriv * gpiodev = dev_get_drvdata(dev);
   if (0 != gpiodev)
   {
      ret = sprintf(buf, "%d\n", gpiodev->intCount);
   }
   return ret;
}
static DEVICE_ATTR_RO(intCount);

static ssize_t lastTime_show(struct device * dev, struct device_attribute *attr, char *buf)
{
   ssize_t ret = 0;
   
   gpioDevPriv * gpiodev = dev_get_drvdata(dev);
   if (0 != gpiodev)
   {
      
      ret = sprintf(buf, "%.2lu:%.2lu:%.2lu:%.9lu\n", (gpiodev->tsLast.tv_sec/3600)%24,
        (gpiodev->tsLast.tv_sec/60)%60, gpiodev->tsLast.tv_sec % 60, 
        gpiodev->tsLast.tv_nsec);
   }
   return ret;
}
static DEVICE_ATTR_RO(lastTime);

static ssize_t diffTime_show(struct device * dev, struct device_attribute *attr, char *buf)
{
   ssize_t ret = 0;
   
   gpioDevPriv * gpiodev = dev_get_drvdata(dev);
   if (0 != gpiodev)
   {
      ret = sprintf(buf, "%lu.%.9lu\n", gpiodev->tsDiff.tv_sec, gpiodev->tsDiff.tv_nsec);
   }
   return ret;
}
static DEVICE_ATTR_RO(diffTime);

static struct attribute *gpioattrs[] = {
      &dev_attr_intCount.attr,    
      &dev_attr_lastTime.attr,
      &dev_attr_diffTime.attr,
      NULL,
};

static struct attribute_group gpioattrgroup = {
      .attrs = gpioattrs,
};

static const struct attribute_group *gpioattrgroups[] = {
      &gpioattrgroup,
      NULL,
};

static void addHandler(int gpioNum)
{
  if((gpioNum >= 0) && (gpioNum < NUM_GPIO))
  {
     gpioDevPriv * gpiodev = &gpioDevice[gpioNum];
     if (gpiodev->gpioNum != gpioNum)
     {
        dev_t currDev;
        struct device * dev;
        int ret;

        unsigned long irqFlags = IRQF_TRIGGER_FALLING;
        
        sprintf(gpiodev->gpioName, "gpio%d", gpioNum);
        gpiodev->gpioNum = gpioNum;

        ret = gpio_request(gpiodev->gpioNum, "sysfs");
        if (0 != ret)
        {
           printk(KERN_ALERT "gpio-irq: unsuccessful request for GPIO %d\n", gpiodev->gpioNum);
        }

        ret = gpio_direction_input(gpiodev->gpioNum);
        if (0 != ret)
        {
           printk(KERN_ALERT "gpio-irq: failed to to make GPIO %d an input\n", gpiodev->gpioNum);
        }

        ret = gpio_export(gpiodev->gpioNum, false); /* false == don't allow user to change direction */
        if (0 != ret)
        {
           printk(KERN_ALERT "gpio-irq: failed to export GPIO %d\n", gpiodev->gpioNum);
        }        

        gpiodev->irqNum = gpio_to_irq(gpiodev->gpioNum);

        if (gpiodev-> irqNum > 0)
        {
           printk(KERN_INFO "gpio-irq: IRQ %d assigned to GPIO %d\n", gpiodev->irqNum, gpiodev->gpioNum);
           request_irq(gpiodev->irqNum, 
             (irq_handler_t) gpioIRQ_Handler,
              irqFlags,
             "gpio_irq_handler",
              gpiodev);
        }
        else
        {
           printk(KERN_ALERT "gpio-irq: could not get irq for GPIO %d\n", gpiodev->gpioNum);
        }

        cdev_init(&gpiodev->gpiocdev, &gpio_fops);
        currDev = MKDEV(MAJOR(devNum), MINOR(devNum) + gpioNum);
        dev = device_create_with_groups(&gpioClass, NULL, currDev,
          gpiodev, gpioattrgroups, gpiodev->gpioName);
        cdev_add(&gpiodev->gpiocdev, currDev, 1);

     }
  }
}

static void removeHandler(int gpioNum)
{
  if((gpioNum >= 0) && (gpioNum < NUM_GPIO))
  {
     gpioDevPriv * gpiodev = &gpioDevice[gpioNum];
     if (gpiodev->gpioNum == gpioNum)
     {
      device_destroy(&gpioClass, MKDEV(MAJOR(devNum), MINOR(devNum) + gpioNum));     

      gpio_unexport(gpiodev->gpioNum);
      gpio_free(gpiodev->gpioNum);

      if (gpiodev->irqNum > 0)
      {
        free_irq(gpiodev->irqNum, gpiodev);
      }

      gpiodev->gpioNum = -1;
      gpiodev->irqNum = -1;
     }
  }
}

static irq_handler_t gpioIRQ_Handler(unsigned int irq, void * dev_id, struct pt_regs * regs)
{
   irq_handler_t ret = (irq_handler_t)IRQ_NONE;

   gpioDevPriv * gpiodev = (gpioDevPriv *)dev_id;

   if ((0 != gpiodev) && (irq == gpiodev->irqNum))
   {
      ret = (irq_handler_t)IRQ_HANDLED;

      gpiodev->tsLast = gpiodev->tsCurrent;
      getnstimeofday(&gpiodev->tsCurrent);
      gpiodev->tsDiff = timespec_sub(gpiodev->tsCurrent, gpiodev->tsLast);
      gpiodev->intCount++;

   }

   return ret;
}

static int __init gpioInit(void)
{
   int i;
   int ret;
   dev_t currDev;
   struct device * dev;
   printk(KERN_INFO "gpio-irq: Initialising the gpio-irq LKM!\n");
   
   ret = alloc_chrdev_region(&devNum, 0, NUM_GPIO + 1, DEVICE_NAME);
   if (ret < 0)
   {
      printk(KERN_ALERT "gpio-irq: Failed to allocate device region\n");
      return ret;
   }
   ret = class_register(&gpioClass);
   if (ret < 0)
   {              
      unregister_chrdev_region(devNum, NUM_GPIO + 1);   
      printk(KERN_ALERT "gpio-irq: Failed to register device class\n");
      return ret;          
   }
   
   for (i = 0; i < NUM_GPIO; i++)
   {
      gpioDevice[i].gpioNum = -1;
      gpioDevice[i].irqNum = -1;
   }
   cdev_init(&drivercdev, &driver_fops);
   currDev = MKDEV(MAJOR(devNum), MINOR(devNum) + NUM_GPIO);
   dev = device_create_with_groups(&gpioClass, NULL, currDev,
      NULL, gpioattrgroups, "gpio_driver_controller");
   if (NULL == dev)
   {
      printk(KERN_ALERT "gpio-irq: Failed to create controller device\n");
      class_unregister(&gpioClass);                            
      unregister_chrdev_region(devNum, NUM_GPIO + 1);   
      return -1;
   }
   ret = cdev_add(&drivercdev, currDev, 1);
   if (ret < 0)
   {
     printk(KERN_ALERT "gpio-irq: Failed to add contorller device\n");
     device_destroy(&gpioClass, currDev);
     class_unregister(&gpioClass);                            
     unregister_chrdev_region(devNum, NUM_GPIO + 1);   
   }

   printk(KERN_INFO "gpio-irq: Initialised\n");
   return 0;
}
 
static void __exit gpioExit(void)
{
   int i;

   for (i = 0; i < NUM_GPIO; i++)
   {
      removeHandler(i);
   }

   /* also cleanup the gpio_driver_controller */
   device_destroy(&gpioClass, MKDEV(MAJOR(devNum), MINOR(devNum) + NUM_GPIO));  

   class_unregister(&gpioClass);                            
   unregister_chrdev_region(devNum, NUM_GPIO + 1);    

   printk(KERN_INFO "gpio-irq: Goodbye from the gpio-irq LKM!\n");
}

module_init(gpioInit);
module_exit(gpioExit);

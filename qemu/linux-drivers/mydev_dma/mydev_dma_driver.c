#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/wait.h>

#define DRIVER_NAME "mydev-dma-driver"
#define MISC_NAME "mydev"
#define DMA_BUF_SIZE 4096

/* offsets must match QEMU device */
#define MYDEV_OFF_CMD 0x00
#define MYDEV_OFF_DMA_GPA_LOW 0x08
#define MYDEV_OFF_DMA_GPA_HIGH 0x0c
#define MYDEV_OFF_DMA_LEN 0x10
#define MYDEV_OFF_STATUS 0x18

/* ioctl commands */
#define MYDEV_IOC_MAGIC 'M'
#define MYDEV_CMD_DEVICE_TO_USER _IO(MYDEV_IOC_MAGIC, 1) /* device -> guest (device writes into buffer) */
#define MYDEV_CMD_USER_TO_DEVICE _IO(MYDEV_IOC_MAGIC, 2) /* guest -> device (device reads from buffer) */

typedef struct
{
    struct device *dev;
    void __iomem *mmio;

    int irq;
    struct completion done; /* signaled by irq handler */

    void *dma_buf;         /* kernel virtual (CPU) addr returned by dma_alloc_coherent */
    dma_addr_t dma_handle; /* dma (physical) address to give to device (guest physical) */
    size_t dma_size;
    struct miscdevice misc; /* /dev/mydev */
} mydev_t;

mydev_t mydev;

static irqreturn_t mydev_irq_thread(int irq, void *devid)
{
    uint32_t status;

    // read status from device; we don't use it much here
    status = readl(mydev.mmio + MYDEV_OFF_STATUS);
    dev_info(mydev.dev, "mydev: IRQ thread - status=%#x\n", status);

    // wake any waiter (ioctl)
    complete(&mydev.done);

    return IRQ_HANDLED;
}

// ioctl - start device DMA and wait for completion
static long mydev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    dev_info(mydev.dev, "mydev: mydev_ioctl cmd: %d\n", cmd);
    int ret;

    switch (cmd)
    {
    case MYDEV_CMD_DEVICE_TO_USER:
        reinit_completion(&mydev.done);

        // write DMA address (split 64-bit)
        writel((uint32_t)(mydev.dma_handle & 0xffffffff), mydev.mmio + MYDEV_OFF_DMA_GPA_LOW);
        writel((uint32_t)((mydev.dma_handle >> 32) & 0xffffffff), mydev.mmio + MYDEV_OFF_DMA_GPA_HIGH);

        // length
        writel((uint32_t)mydev.dma_size, mydev.mmio + MYDEV_OFF_DMA_LEN);

        // Trigger device -> guest DMA (CMD=1)
        writel(1, mydev.mmio + MYDEV_OFF_CMD);

        // wait for completion (interrupt)
        ret = wait_for_completion_interruptible_timeout(&mydev.done, msecs_to_jiffies(5000));
        if (ret == 0)
        {
            dev_err(mydev.dev, "mydev: timeout waiting for DMA completion\n");
            return -ETIMEDOUT;
        }
        else if (ret < 0)
        {
            return ret;
        }
        return 0;
    case MYDEV_CMD_USER_TO_DEVICE:
        reinit_completion(&mydev.done);

        // user filled the mapped buffer; now ask device to read from guest (CMD=2)
        writel((uint32_t)(mydev.dma_handle & 0xffffffff), mydev.mmio + MYDEV_OFF_DMA_GPA_LOW);
        writel((uint32_t)((mydev.dma_handle >> 32) & 0xffffffff), mydev.mmio + MYDEV_OFF_DMA_GPA_HIGH);
        writel((uint32_t)mydev.dma_size, mydev.mmio + MYDEV_OFF_DMA_LEN);

        // Trigger guest -> device DMA (CMD=2)
        writel(2, mydev.mmio + MYDEV_OFF_CMD);

        ret = wait_for_completion_interruptible_timeout(&mydev.done, msecs_to_jiffies(5000));
        if (ret == 0)
        {
            dev_err(mydev.dev, "mydev: timeout waiting for DMA completion\n");
            return -ETIMEDOUT;
        }
        else if (ret < 0)
        {
            return ret;
        }
        return 0;
    default:
        return -ENOTTY;
    }
}

// mmap: expose coherent DMA buffer to userspace
static int mydev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    // struct mydev *m = container_of(filp->private_data, struct mydev, misc);
    unsigned long size = vma->vm_end - vma->vm_start;
    dev_info(mydev.dev, "mydev_mmap size: %#lx dma_size: %#lx\n", size, mydev.dma_size);
    if (size > mydev.dma_size)
        return -EINVAL;

    // dma_mmap_coherent maps a DMA coherent buffer into userspace and handles
    // cache attributes appropriately.
    if (dma_mmap_coherent(mydev.dev, vma, mydev.dma_buf, mydev.dma_handle, mydev.dma_size) < 0)
        return -EFAULT;

    return 0;
}

static int mydev_open(struct inode *inode, struct file *filp)
{
    // provide file->private_data so file ops can access mydev
    // struct mydev *m = container_of(container_of(inode->i_cdev, struct miscdevice, minor), struct mydev, misc);
    // struct mydev *m = container_of(filp->private_data, struct mydev, misc);
    dev_info(mydev.dev, "mydev_open\n");
    filp->private_data = &mydev;

    return 0;
}

static const struct file_operations mydev_fops = {
    .owner = THIS_MODULE,
    .open = mydev_open,
    .unlocked_ioctl = mydev_ioctl,
    .mmap = mydev_mmap,
    /* you can add .release if needed */
};

static int mydev_probe(struct platform_device *pdev)
{
    struct resource *res;
    int ret;

    // struct mydev *m;
    // m = devm_kzalloc(&pdev->dev, sizeof(*m), GFP_KERNEL);
    // if (!m)
    //     return -ENOMEM;
    mydev.dev = &pdev->dev;
    platform_set_drvdata(pdev, &mydev);

    // MMIO mapping
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res)
    {
        dev_err(&pdev->dev, "no MMIO resource\n");
        return -ENODEV;
    }

    mydev.mmio = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(mydev.mmio))
        return PTR_ERR(mydev.mmio);

    // allocate a DMA coherent buffer
    mydev.dma_size = DMA_BUF_SIZE;
    mydev.dma_buf = dma_alloc_coherent(mydev.dev, mydev.dma_size, &mydev.dma_handle, GFP_KERNEL);
    if (!mydev.dma_buf)
    {
        dev_err(mydev.dev, "dma_alloc_coherent failed\n");
        return -ENOMEM;
    }
    dev_info(mydev.dev, "DMA buffer: cpu=%p dma=%pad size=%zu\n",
             mydev.dma_buf, &mydev.dma_handle, mydev.dma_size);

    // request irq
    mydev.irq = platform_get_irq(pdev, 0);
    if (mydev.irq < 0)
    {
        dev_err(mydev.dev, "no irq\n");
        ret = mydev.irq;
        goto err_free_dma;
    }

    init_completion(&mydev.done);

    ret = devm_request_threaded_irq(&pdev->dev, mydev.irq, NULL, mydev_irq_thread,
                                    IRQF_ONESHOT, DRIVER_NAME, &mydev);
    if (ret)
    {
        dev_err(mydev.dev, "request irq failed: %d\n", ret);
        goto err_free_dma;
    }

    // register misc device
    mydev.misc.minor = MISC_DYNAMIC_MINOR;
    mydev.misc.name = MISC_NAME;
    mydev.misc.fops = &mydev_fops;
    mydev.misc.parent = mydev.dev;
    ret = misc_register(&mydev.misc);
    if (ret)
    {
        dev_err(mydev.dev, "misc_register failed: %d\n", ret);
        goto err_free_dma;
    }

    dev_info(mydev.dev, "mydev_dma probe OK, /dev/%s riq %d ready\n", MISC_NAME, mydev.irq);
    return 0;

err_free_dma:
    dma_free_coherent(mydev.dev, mydev.dma_size, mydev.dma_buf, mydev.dma_handle);
    return ret;
}

static int mydev_remove(struct platform_device *pdev)
{
    // struct mydev *m = platform_get_drvdata(pdev);
    misc_deregister(&mydev.misc);
    if (mydev.dma_buf)
        dma_free_coherent(mydev.dev, mydev.dma_size, mydev.dma_buf, mydev.dma_handle);

    return 0;
}

static const struct of_device_id mydev_of_match[] = {
    {
        .compatible = "mycompany,mydev-dma",
    },
    {},
};
MODULE_DEVICE_TABLE(of, mydev_of_match);

static struct platform_driver mydev_driver = {
    .probe = mydev_probe,
    .remove = mydev_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = mydev_of_match,
    },
};

module_platform_driver(mydev_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sungkeun Kim");
MODULE_DESCRIPTION("DMA Device for WorldClave");

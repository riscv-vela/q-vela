
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define DRIVER_NAME "my-device-driver"
#define DEFAULT_PHYS_ADDR 0x006003000ULL
#define DEFAULT_SIZE 0x1000
#define REG0_OFFSET 0x0

struct mydev
{
    struct device *dev;
    int irq;
    void __iomem *regs;
    resource_size_t size;

    struct work_struct irq_work;
    atomic_t irq_count;
};

static void mydev_irq_work_fn(struct work_struct *work)
{
    struct mydev *m = container_of(work, struct mydev, irq_work);

    pr_info("%s: IRQ bottom half: handled (count=%d)\n", DRIVER_NAME, atomic_read(&m->irq_count));
    u32 val = ioread32(m->regs + REG0_OFFSET);
    pr_info("%s: read offset 0x0 - %#x\n", DRIVER_NAME, val);
}

static irqreturn_t mydev_threaded_irq(int irq, void *dev_id)
{
    struct mydev *m = dev_id;
    atomic_inc(&m->irq_count);
    schedule_work(&m->irq_work);
    return IRQ_HANDLED;
}

static int mydev_probe(struct platform_device *pdev)
{
    struct mydev *m;
    int irq;
    int err;

    pr_info("%s: probe start\n", DRIVER_NAME);

    m = devm_kzalloc(&pdev->dev, sizeof(*m), GFP_KERNEL);
    if (!m)
        return -ENOMEM;
    m->dev = &pdev->dev;
    platform_set_drvdata(pdev, m);

    atomic_set(&m->irq_count, 0);
    INIT_WORK(&m->irq_work, mydev_irq_work_fn);

    struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    m->regs = devm_ioremap_resource(&pdev->dev, res);

    irq = platform_get_irq(pdev, 0);
    if (irq < 0)
    {
        pr_info("%s: failed to get IRQ: %d\n", DRIVER_NAME, irq);
        return irq;
    }

    m->irq = irq;

    err = devm_request_threaded_irq(&pdev->dev,
                                    m->irq,
                                    NULL,               /*primary (top) handler*/
                                    mydev_threaded_irq, /*threaded handler*/
                                    IRQF_ONESHOT,       /*flags*/
                                    DRIVER_NAME,
                                    m);
    if (err)
    {
        pr_info("%s: request_threaded_irq failed: %d\n", DRIVER_NAME, err);
        return err;
    }

    pr_info("%s: requested irq: %d\n", DRIVER_NAME, m->irq);

    /** Optionally mask/enable/power-down/up using PM calls here:
     * pm_runtime_enable(m->dev);
     * pm_runtime_get_sync(m->dev);
     */

    pr_info("%s:probe OK\n", DRIVER_NAME);
    iowrite32(1, m->regs + 0x4);
    return 0;
}

static int mydev_remove(struct platform_device *pdev)
{
    struct mydev *m = platform_get_drvdata(pdev);

    pr_info("%s: remove\n", DRIVER_NAME);

    cancel_work_sync(&m->irq_work);

    return 0;
}

static const struct of_device_id mydev_of_match[] = {
    {
        .compatible = "mycompany,my-device",
    },
    {/*sentinel*/}};

MODULE_DEVICE_TABLE(of, mydev_of_match);

static struct platform_driver mydev_platform_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = mydev_of_match,
    },
    .probe = mydev_probe,
    .remove = mydev_remove,
};

module_platform_driver(mydev_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("Simple MMIO driver");

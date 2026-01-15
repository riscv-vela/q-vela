#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qom/object.h"

#define TYPE_MY_DEVICE "my-device"
#define MY_DEVICE(obj) OBJECT_CHECK(MyDeviceState, (obj), TYPE_MY_DEVICE)

// Register Map
#define REG_ID 0x0
#define CHIP_ID 0xf001

#define REG_INIT 0x4
#define CHIP_EN BIT(0)

#define REG_CMD 0x8

#define REG_INT_STATUS 0xc
#define INT_ENABLED BIT(0)
#define INT_BUFFER_DEQ BIT(1)

typedef struct
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t id;
    uint32_t init;
    uint32_t cmd;
    uint32_t status;
    qemu_irq irq;
} MyDeviceState;

static void my_device_set_irq(MyDeviceState *s, int irq)
{
    s->status = irq;
    qemu_set_irq(s->irq, 1);
}

static void my_device_clr_irq(MyDeviceState *s)
{
    qemu_set_irq(s->irq, 0);
}

static uint64_t my_device_read(void *opaque, hwaddr offset, unsigned size)
{
    printf("device read offset: %#lx size: %#x\n", offset, size);
    MyDeviceState *s = (MyDeviceState *)opaque;
    bool is_enabled = s->init & CHIP_EN;

    if (!is_enabled)
    {
        fprintf(stderr, "Device is disabled\n");
        return 0;
    }

    switch (offset)
    {
    case REG_ID:
        return s->id;
    case REG_INIT:
        return s->init;
    case REG_CMD:
        return s->cmd;
    case REG_INT_STATUS:
        my_device_clr_irq(s);
        return s->status;
    default:
        break;
    }

    return 0;
}

static void my_device_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    printf("device write offset: %#lx size: %#x value: %ld\n", offset, size, value);
    MyDeviceState *s = (MyDeviceState *)opaque;

    switch (offset)
    {
    case REG_INIT:
        s->init = (int)value;
        if (value)
            my_device_set_irq(s, INT_ENABLED);
        break;
    case REG_CMD:
        s->cmd = (int)value;
        my_device_set_irq(s, INT_BUFFER_DEQ);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps my_device_ops = {
    .read = my_device_read,
    .write = my_device_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void my_device_realize(DeviceState *d, Error **errp)
{
    MyDeviceState *s = MY_DEVICE(d);
    SysBusDevice *sbd = SYS_BUS_DEVICE(d);

    memory_region_init_io(&s->iomem, OBJECT(s), &my_device_ops, s, TYPE_MY_DEVICE, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    sysbus_mmio_map(SYS_BUS_DEVICE(d), 0, 0x06003000);

    s->id = CHIP_ID;
    s->init = 0;

    // mydevice_add_fdt_node(SYS_BUS_DEVICE(d));

    qemu_log("mydevice: realized and mapped at 0x%012" PRIx64 " size 0x%x\n",
             (uint64_t)0x06003000,
             0x1000);
}

static void my_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = my_device_realize;
}

static const TypeInfo my_device_info = {
    .name = TYPE_MY_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyDeviceState),
    .class_init = my_device_class_init,
};

static void my_device_register_types(void)
{
    type_register_static(&my_device_info);
}

type_init(my_device_register_types)

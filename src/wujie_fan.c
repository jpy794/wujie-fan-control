#include "linux/kobject.h"
#include "linux/mutex.h"
#include <asm/io.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jpy794");
MODULE_DESCRIPTION("Mechrevo Wujie 16 laptop fan control");

struct ec_register_offsets {
    u16 FAN1_RPM_LSB;
    u16 FAN1_RPM_MSB;
    u16 FAN2_RPM_LSB;
    u16 FAN2_RPM_MSB;

    u16 EXT_FAN_CTRL_EN;
    u16 EXT_FAN1_TARGET_RPM;
    u16 EXT_FAN2_TARGET_RPM;
    u16 EXT_CPU_TEMP;
    u16 EXT_ENV_TEMP;
};

static const struct ec_register_offsets ec_register_offsets_v0 = {
    .FAN1_RPM_LSB = 0x181e,
    .FAN1_RPM_MSB = 0x181f,
    .FAN2_RPM_LSB = 0x1820,
    .FAN2_RPM_MSB = 0x1821,

    .EXT_FAN_CTRL_EN = 0xd130,
    .EXT_FAN1_TARGET_RPM = 0xd16f,
    .EXT_FAN2_TARGET_RPM = 0xd133,
    .EXT_CPU_TEMP = 0xd118,
    .EXT_ENV_TEMP = 0xd115,
};

/* ec port io */

#define IO_PORT_BASE 0x4e
#define IO_PORT_NUM 2

int ec_portio_init(void) {
    if (!request_region(IO_PORT_BASE, IO_PORT_NUM, "wujie-fan")) {
        pr_info("Failed to init I/O port\n");
        return -ENODEV;
    }
    return 0;
}

void ec_portio_exit(void) { release_region(IO_PORT_BASE, IO_PORT_NUM); }

#define PNP_ADDR 0x4e
#define PNP_DATA 0x4f

#define SUPERIO_ADDR 0x2e
#define SUPERIO_DATA 0x2f

#define I2EC_ADDR_L 0x10
#define I2EC_ADDR_H 0x11
#define I2EC_DATA 0x12

static void pnp_write(u8 addr, u8 data) {
    outb(addr, PNP_ADDR);
    outb(data, PNP_DATA);
}

static u8 pnp_read(u8 addr) {
    outb(addr, PNP_ADDR);
    return inb(PNP_DATA);
}

static void superio_write(u8 addr, u8 data) {
    pnp_write(SUPERIO_ADDR, addr);
    pnp_write(SUPERIO_DATA, data);
}

static u8 superio_read(u8 addr) {
    pnp_write(SUPERIO_ADDR, addr);
    return pnp_read(SUPERIO_DATA);
}

static DEFINE_MUTEX(i2ec_mutex);

static void i2ec_write(u16 addr, u8 data) {
    superio_write(I2EC_ADDR_L, addr & 0xff);
    superio_write(I2EC_ADDR_H, addr >> 8);
    superio_write(I2EC_DATA, data);
}

static u8 i2ec_read(u16 addr) {
    superio_write(I2EC_ADDR_L, addr & 0xff);
    superio_write(I2EC_ADDR_H, addr >> 8);
    return superio_read(I2EC_DATA);
}

/* fan speed */

int fanctrl_enabled(void) {
    u8 enable;

    mutex_lock(&i2ec_mutex);
    enable = i2ec_read(ec_register_offsets_v0.EXT_FAN_CTRL_EN);
    mutex_unlock(&i2ec_mutex);

    return enable;
}

int read_fan_speed(size_t fan_id) {
    u8 lsb, msb;
    u16 lsb_addr, msb_addr;
    u16 speed;

    switch (fan_id) {
    case 1:
        lsb_addr = ec_register_offsets_v0.FAN1_RPM_LSB;
        msb_addr = ec_register_offsets_v0.FAN1_RPM_MSB;
        break;
    case 2:
        lsb_addr = ec_register_offsets_v0.FAN2_RPM_LSB;
        msb_addr = ec_register_offsets_v0.FAN2_RPM_MSB;
        break;
    default:
        return -EINVAL;
    }

    mutex_lock(&i2ec_mutex);
    msb = i2ec_read(msb_addr);
    lsb = i2ec_read(lsb_addr);
    mutex_unlock(&i2ec_mutex);

    speed = ((u16)msb << 8) | lsb;

    if (speed == 0 || speed >= 0x4000) {
        return 0;
    }
    if (speed < 0x80) {
        return 9999;
    }
    return 2156250 / speed;
}

int write_fan_speed(size_t fan_id, u8 speed) {
    u16 addr;

    switch (fan_id) {
    case 1:
        addr = ec_register_offsets_v0.EXT_FAN1_TARGET_RPM;
        break;
    case 2:
        addr = ec_register_offsets_v0.EXT_FAN2_TARGET_RPM;
        break;
    default:
        return -EINVAL;
    }

    mutex_lock(&i2ec_mutex);
    i2ec_write(addr, speed);
    mutex_unlock(&i2ec_mutex);

    return 0;
}

/* sysfs */

static int fan_show(size_t fan_id, char *buf) {
    int speed;

    speed = read_fan_speed(fan_id);
    if (speed < 0) {
        return -EINVAL;
    }

    return sprintf(buf, "%d", speed);
}

static int fan_store(size_t fan_id, const char *buf, size_t count) {
    int speed, err;

    if (sscanf(buf, "%d", &speed) != 1) {
        return -EINVAL;
    }

    /* clamp */
    if (speed > 127) {
        speed = 127;
    } else if (speed < 0) {
        speed = 0;
    }

    err = write_fan_speed(fan_id, speed);
    if (err) {
        return err;
    }

    return count;
}

static ssize_t
fan1_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return fan_show(1, buf);
}

static ssize_t fan1_store(
    struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
    size_t count
) {
    return fan_store(1, buf, count);
}

static struct kobj_attribute fan1_attr = __ATTR_RW(fan1);

static ssize_t
fan2_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return fan_show(2, buf);
}

static ssize_t fan2_store(
    struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
    size_t count
) {
    return fan_store(2, buf, count);
}

static struct kobj_attribute fan2_attr = __ATTR_RW(fan2);

static ssize_t
fanctl_en_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d", fanctrl_enabled());
}

static ssize_t fanctl_en_store(
    struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
    size_t count
) {
    int enable;

    if (sscanf(buf, "%d", &enable) != 1) {
        return -EINVAL;
    }

    mutex_lock(&i2ec_mutex);
    i2ec_write(ec_register_offsets_v0.EXT_FAN_CTRL_EN, enable ? 1 : 0);
    mutex_unlock(&i2ec_mutex);

    return count;
}

static struct kobj_attribute fanctl_en_attr = __ATTR_RW(fanctl_en);

static struct kobject *wujie_fan_kobj;
static struct attribute *wujie_fan_attrs[] = {
    &fan1_attr.attr,
    &fan2_attr.attr,
    &fanctl_en_attr.attr,
    NULL,
};
static struct attribute_group wujie_fan_attr_group = {
    .name = "wujie_fan",
    .attrs = wujie_fan_attrs,
};

static int __init wujie_fan_init(void) {
    int err = 0;

    pr_info("Initializing\n");
    err = ec_portio_init();
    if (err) {
        goto error;
    }

    wujie_fan_kobj = kobject_create_and_add("wujie_fan", kernel_kobj);
    err = sysfs_create_group(wujie_fan_kobj, &wujie_fan_attr_group);
    if (err) {
        goto error;
    }

    return 0;

error:
    pr_info("Failed to init wujie-fan\n");
    return err;
}

static void __exit wujie_fan_exit(void) {
    pr_info("Exiting\n");
    ec_portio_exit();
    kobject_put(wujie_fan_kobj);
}

module_init(wujie_fan_init);
module_exit(wujie_fan_exit);

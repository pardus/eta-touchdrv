#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/input/mt.h>

#include "OtdDrv.h"

#define DRIVER_NAME     "Optical touch device"
#define DEVICE_NAME     "Otd"

#define err(format, arg...)                \
    printk(KERN_ERR KBUILD_MODNAME ": " format "\n", ##arg)
#define info(format, arg...)                \
    printk(KERN_INFO KBUILD_MODNAME ": " format "\n", ##arg)


typedef struct _device_context_pool
{
    char name[128];
    char phys[64];
}
device_context_pool;

typedef struct _device_context
{
    char inBuff[64];

    struct usb_device *usb_device;
    struct input_dev *input_dev;
    struct device* device;
    struct cdev cdev;
    device_context_pool pool;
    dev_t dev;
    int pipe_input;
}
device_context;


static struct usb_device_id const dev_table[] =
{
    { USB_DEVICE(0x2621, 0x2201) },
    { USB_DEVICE(0x2621, 0x4501) },
    {}
};

static struct class *otd_class;

static ssize_t otd_read(struct file * filp, char * buffer, size_t count, loff_t * ppos)
{
    int retval;
    int bytes_read;
    device_context * otd;

    otd = filp->private_data;

    if (otd == NULL)
    {
        err("%s: private_data is NULL!", __func__);
        return -EFAULT;
    }

    retval = usb_interrupt_msg(otd->usb_device, otd->pipe_input, otd->inBuff, sizeof(otd->inBuff), &bytes_read, 1000);
    if (retval == -ETIMEDOUT)
    {
        return 0;
    }
    if (retval != 0)
    {
        return -EFAULT;
    }
    if (bytes_read > count)
    {
        bytes_read = count;
    }
    if (raw_copy_to_user(buffer, otd->inBuff, bytes_read) != 0)
    {
        return -EFAULT;
    }
    return bytes_read;
}

static ssize_t otd_write(struct file * filp, const char * user_buffer, size_t count, loff_t * ppos)
{
    device_context *otd;

    otd = filp->private_data;

    if (otd == NULL)
    {
        err("%s: private_data is NULL!", __func__);
        return -EFAULT;
    }

    return -EFAULT;
}

static long set_report(device_context *otd, unsigned short length, void const* data)
{
    void* kernel_data;
    int r;

    kernel_data = kmalloc(length, GFP_KERNEL);
    if (kernel_data == NULL)
    {
        return -ENOMEM;
    }
    do
    {
        r = raw_copy_from_user(kernel_data, data, length);
        if (r != 0)
        {
            break;
        }
        if (length < 1)
        {
            break;
        }
        r = usb_control_msg(otd->usb_device, usb_sndctrlpipe(otd->usb_device, 0), 0, 0x40, 0x300 | (((unsigned char*)kernel_data)[0] & 0xff), 0, kernel_data, length, 1000);
        kfree(kernel_data);
        return r;
    } while (false);
    kfree(kernel_data);
    return -EFAULT;
}
static long get_report(device_context *otd, unsigned short length, void* data)
{
    void* kernel_data;
    int r;

    kernel_data = kmalloc(length, GFP_KERNEL);
    if (kernel_data == NULL)
    {
        return -ENOMEM;
    }
    do
    {
        if (length < 1)
        {
            break;
        }
        r = usb_control_msg(otd->usb_device, usb_rcvctrlpipe(otd->usb_device, 0), 0, 0xc0, 0x300 | (((unsigned char const*)kernel_data)[0] & 0xff), 0, kernel_data, length, 1000);
        if (r >= 0)
        {
            if (raw_copy_to_user(data, kernel_data, r) != 0)
            {
                break;
            }
        }
        kfree(kernel_data);
        return r;
    } while (false);
    kfree(kernel_data);
    return -EFAULT;
}
static long sync_multitouch(device_context *otd, unsigned short length, void const* data)
{
    OtdReportPacketMultiTouch value;
    int i;
    int r;

    if (length < sizeof(value))
    {
        return 0;
    }
    r = raw_copy_from_user(&value, data, sizeof(value));
    if (r != 0)
    {
        return 0;
    }
    for (i = 0; i < sizeof(value.touchPoints) / sizeof(value.touchPoints[0]); i++)
    {
        if ((value.touchPoints[i].state & OtdReportTouchPointStateFlag_IsValid) == 0)
        {
            continue;
        }
        input_mt_slot(otd->input_dev, i);
        if ((value.touchPoints[i].state & OtdReportTouchPointStateFlag_IsTouched) != 0)
        {
            input_mt_report_slot_state(otd->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(otd->input_dev, ABS_MT_TOUCH_MAJOR, value.touchPoints[i].width);
            input_report_abs(otd->input_dev, ABS_MT_TOUCH_MINOR, value.touchPoints[i].height);
            input_report_abs(otd->input_dev, ABS_MT_POSITION_X, value.touchPoints[i].x);
            input_report_abs(otd->input_dev, ABS_MT_POSITION_Y, value.touchPoints[i].y);
        }
        else
        {
            input_mt_report_slot_state(otd->input_dev, MT_TOOL_FINGER, false);
        }
    }
    input_sync(otd->input_dev);
    return sizeof(value);
}
static long otd_unlocked_ioctl(struct file * filp, unsigned int ctl_code, unsigned long ctl_param)
{
    device_context *otd;

    otd = filp->private_data;
    if (otd == NULL)
    {
        err("%s: private_data is NULL!", __func__);
        return -EFAULT;
    }

    switch (ctl_code & OTD_IOCTL_CODE_TYPE_MASK)
    {
    case OTD_IOCTL_CODE_TYPE_SET_REPORT:
        return set_report(otd, ctl_code & OTD_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OTD_IOCTL_CODE_TYPE_GET_REPORT:
        return get_report(otd, ctl_code & OTD_IOCTL_CODE_LENGTH_MASK, (void*)ctl_param);
    case OTD_IOCTL_CODE_TYPE_SYNC_MULTITOUCH:
        return sync_multitouch(otd, ctl_code & OTD_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    }
    return 0;
}

static int otd_open(struct inode * inode, struct file * filp)
{
    device_context* otd;

    if (inode->i_cdev == NULL)
    {
        err("%s: dev ptr is NULL.", __func__);
        return -1;
    }

    otd = container_of(inode->i_cdev, device_context, cdev);
    filp->private_data = otd;

    return 0;
}

static int otd_release(struct inode * inode, struct file * filp)
{
    return 0;
}

static struct file_operations otd_fops =
{
    .owner = THIS_MODULE,
    .read = otd_read,
    .write = otd_write,
    .unlocked_ioctl = otd_unlocked_ioctl,
    .open = otd_open,
    .release = otd_release,
};

static int otd_open_device(struct input_dev * input_dev)
{
    info("%s", __func__);
    return 0;
}

static void otd_close_device(struct input_dev * input_dev)
{
    info("%s", __func__);
}

static void device_context_init(device_context* obj, struct usb_interface* intf)
{
    int i;

    obj->usb_device = interface_to_usbdev(intf);

    for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++)
    {
        if (intf->cur_altsetting->endpoint[i].desc.bEndpointAddress & USB_DIR_IN)
        {
            obj->pipe_input = usb_rcvintpipe(obj->usb_device, intf->cur_altsetting->endpoint[i].desc.bEndpointAddress);
        }
    }
}
static void input_dev_init(struct input_dev* obj, device_context_pool* pool, struct usb_device* usb_device, struct device* parent)
{
    if (usb_device->manufacturer != NULL)
    {
        strlcpy(pool->name, usb_device->manufacturer, sizeof(pool->name));
    }
    else
    {
        pool->name[0] = 0;
    }

    if (usb_device->product != NULL)
    {
        strlcat(pool->name, " ", sizeof(pool->name));
        strlcat(pool->name, usb_device->product, sizeof(pool->name));
    }

    if (strlen(pool->name) == 0)
    {
        snprintf(pool->name, sizeof(pool->name), "Optical touch device %04x:%04x", le16_to_cpu(usb_device->descriptor.idVendor), le16_to_cpu(usb_device->descriptor.idProduct));
    }

    usb_make_path(usb_device, pool->phys, sizeof(pool->phys));
    strlcat(pool->phys, "/input0", sizeof(pool->phys));

    obj->name = pool->name;
    obj->phys = pool->phys;

    usb_to_input_id(usb_device, &obj->id);
    obj->dev.parent = parent;

    //没看出实际用途
    //input_set_drvdata(obj, otd);

    obj->open = otd_open_device;
    obj->close = otd_close_device;

    obj->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
    set_bit(BTN_TOUCH, obj->keybit);
    set_bit(EV_SYN, obj->evbit);
    set_bit(EV_KEY, obj->evbit);
    set_bit(EV_ABS, obj->evbit);
    obj->absbit[0] = BIT(ABS_MT_PRESSURE) | BIT(ABS_MT_POSITION_X) | BIT(ABS_MT_POSITION_Y) | BIT(ABS_MT_TOUCH_MAJOR) | BIT(ABS_MT_TOUCH_MINOR);

    input_set_abs_params(obj, ABS_MT_PRESSURE, 0, 1, 0, 0);
    input_set_abs_params(obj, ABS_MT_POSITION_X, 0, 32767, 0, 0);
    input_set_abs_params(obj, ABS_MT_POSITION_Y, 0, 32767, 0, 0);
    input_set_abs_params(obj, ABS_MT_TOUCH_MAJOR, 0, 32767, 0, 0);
    input_set_abs_params(obj, ABS_MT_TOUCH_MINOR, 0, 32767, 0, 0);
    input_mt_init_slots(obj, OTD_TOUCH_POINT_COUNT, INPUT_MT_DIRECT);
}

static int otd_probe(struct usb_interface * intf, const struct usb_device_id *id)
{
    int retval;
    device_context * otd;

    do
    {
        if (IS_ERR(otd_class))
        {
            err("%s: Create class failed.", __func__);
            break;
        }
        otd = kzalloc(sizeof(device_context), GFP_KERNEL);
        if (otd == NULL)
        {
            err("%s: Out of memory.", __func__);
            break;
        }
        do
        {
            device_context_init(otd, intf);
            otd->input_dev = input_allocate_device();
            if (otd->input_dev == NULL)
            {
                break;
            }
            do
            {
                input_dev_init(otd->input_dev, &otd->pool, otd->usb_device, &intf->dev);
                retval = input_register_device(otd->input_dev);
                if (retval != 0)
                {
                    break;
                }
                do
                {
                    usb_set_intfdata(intf, otd);
                    do
                    {
                        msleep(3000);
                        retval = alloc_chrdev_region(&otd->dev, 0, 1, DEVICE_NAME);
                        if (retval != 0)
                        {
                            err("%s: Register chrdev error.", __func__);
                            break;
                        }
                        do
                        {
                            cdev_init(&otd->cdev, &otd_fops);
                            otd->cdev.owner = THIS_MODULE;
                            //由cdev_init完成ops赋值
                            //otd->cdev.ops = &otd_fops;
                            retval = cdev_add(&otd->cdev, otd->dev, 1);

                            if (retval != 0)
                            {
                                err("%s: Adding char_reg_setup_cdev error=%d", __func__, retval);
                                break;
                            }

                            //所有设备用同一结点
                            otd->device = device_create(otd_class, NULL, otd->dev, NULL, DEVICE_NODE_NAME);
                            if (IS_ERR(otd->device))
                            {
                                break;
                            }
                            do
                            {
                                return 0;
                            } while (false);
                            device_destroy(otd_class, otd->dev);
                        } while (false);
                        unregister_chrdev_region(otd->dev, 1);
                    } while (false);
                    usb_set_intfdata(intf, NULL);
                } while (false);
                //原来没有调用input_unregister_device
                input_unregister_device(otd->input_dev);
            } while (false);
            input_free_device(otd->input_dev);
        } while (false);
        kfree(otd);
    } while (false);
    return -ENOMEM;
}

static void otd_disconnect(struct usb_interface * intf)
{
    device_context * otd = usb_get_intfdata(intf);

    //cdev并非源于cdev_alloc，删除可能导致内存泄漏
    //cdev_del(&otd->cdev);
    device_destroy(otd_class, otd->dev);
    unregister_chrdev_region(otd->dev, 1);
    usb_set_intfdata(intf, NULL);
    input_unregister_device(otd->input_dev);
    //原来没有调用input_free_device
    input_free_device(otd->input_dev);
    kfree(otd);
}

static struct usb_driver otd_driver =
{
    .name = DRIVER_NAME,
    .probe = otd_probe,
    .disconnect = otd_disconnect,
    .id_table = dev_table,
};

static int otd_init(void)
{
    int result;

    //之前在otd_mkdev内
    otd_class = class_create(THIS_MODULE, DEVICE_NODE_NAME);
    if (IS_ERR(otd_class))
    {
        err("%s: Class create failed.", __func__);
    }

    /*register this dirver with the USB subsystem*/
    result = usb_register(&otd_driver);
    if (result != 0)
    {
        err("%s: usb_register failed. Error number %d", __func__, result);
    }

    return 0;
}
static void otd_exit(void)
{
    usb_deregister(&otd_driver);
    class_destroy(otd_class);
}

module_init(otd_init);
module_exit(otd_exit);

MODULE_DESCRIPTION("USB driver for Optical touch screen");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Optical touch screen");

// necessary ?
MODULE_DEVICE_TABLE(usb, dev_table);

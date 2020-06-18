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

#include "OpticalDrv.h"

#define DRIVER_NAME     "IRTOUCH optical"

#define err(format, arg...)                \
    printk(KERN_ERR KBUILD_MODNAME ": " format "\n", ##arg)
#define info(format, arg...)                \
    printk(KERN_INFO KBUILD_MODNAME ": " format "\n", ##arg)

#define OPTICAL_MINOR_BASE    0

typedef struct _device_context_pool
{
    char name[128];
    char phys[64];
}
device_context_pool;

typedef struct _device_context
{
    struct usb_device *usb_device;
    struct input_dev *input_dev;
    struct device* device;
    dev_t dev;
    void** file_private_data;
    int pipe_input;
    unsigned char pipe_interval;

    struct urb* interrupt_urb;

    spinlock_t lock;

    unsigned char *ongoing_buffer;
    dma_addr_t ongoing_buffer_dma;

    unsigned char buffer_length;
    unsigned char buffer[64];

    device_context_pool pool;
}
device_context;


static struct usb_device_id const dev_table[] =
{
    { USB_DEVICE(0x6615, 0x0084) },
    { USB_DEVICE(0x6615, 0x0085) },
    { USB_DEVICE(0x6615, 0x0086) },
    { USB_DEVICE(0x6615, 0x0087) },
    { USB_DEVICE(0x6615, 0x0088) },
    { USB_DEVICE(0x6615, 0x0c20) },
    {}
};

static struct usb_driver optical_driver;
static struct file_operations optical_fops;
static struct usb_driver optical_driver;
static struct usb_class_driver optical_class = {
    .name = DEVICE_NODE_FORMAT,
    .fops = &optical_fops,
    .minor_base = OPTICAL_MINOR_BASE,
};

static void submit_urb(device_context* device)
{
    int retval;

    retval = usb_submit_urb(device->interrupt_urb, GFP_KERNEL);
    if (retval != 0)
    {
        return;
    }
}
static void cancel_urb(device_context* device)
{
    usb_kill_urb(device->interrupt_urb);
}

static ssize_t optical_read(struct file * filp, char * buffer, size_t count, loff_t * ppos)
{
    ssize_t r;
    device_context * device;

    device = filp->private_data;
    if (device == NULL)
    {
        return -EFAULT;
    }

    spin_lock_irq(&device->lock);
    do
    {
        if (device->buffer_length <= 0)
        {
            r = 0;
            break;
        }
        if (count > device->buffer_length)
        {
            count = device->buffer_length;
        }
        device->buffer_length = 0;
        if (raw_copy_to_user(buffer, device->buffer, count) != 0)
        {
            r = -EFAULT;
            break;
        }
        r = count;
    } while (false);
    spin_unlock_irq(&device->lock);

    return r;
}

static ssize_t optical_write(struct file * filp, const char * user_buffer, size_t count, loff_t * ppos)
{
    device_context *device;

    device = filp->private_data;
    if (device == NULL)
    {
        return -EFAULT;
    }

    return -EFAULT;
}

static long set_report(device_context *device, unsigned short length, void const* data)
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
        r = usb_control_msg(device->usb_device, usb_sndctrlpipe(device->usb_device, 0), 0, 0x40, 0, 0, kernel_data, length, 1000);
        kfree(kernel_data);
        return r;
    } while (false);
    kfree(kernel_data);
    return -EFAULT;
}
static long get_report(device_context *device, unsigned short length, void* data)
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
        r = usb_control_msg(device->usb_device, usb_rcvctrlpipe(device->usb_device, 0), 0, 0xc0, 0, 0, kernel_data, length, 1000);
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
static long sync_absolute_mouse(device_context *device, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_singletouch(device_context *device, unsigned short length, void const* data)
{
    OpticalReportPacketSingleTouch value;
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
    if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsValid) == 0)
    {
        return sizeof(value);
    }
    input_mt_slot(device->input_dev, 0);
    if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsTouched) != 0)
    {
        input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, true);
        input_report_abs(device->input_dev, ABS_MT_TOUCH_MAJOR, value.touchPoint.width);
        input_report_abs(device->input_dev, ABS_MT_TOUCH_MINOR, value.touchPoint.height);
        input_report_abs(device->input_dev, ABS_MT_POSITION_X, value.touchPoint.x);
        input_report_abs(device->input_dev, ABS_MT_POSITION_Y, value.touchPoint.y);
    }
    else
    {
        input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
    }
    input_sync(device->input_dev);
    return sizeof(value);
}
static long sync_multitouch(device_context *device, unsigned short length, void const* data)
{
    OpticalReportPacketMultiTouch value;
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
    for (i = 0; i < sizeof(value.touchPoint) / sizeof(value.touchPoint[0]); i++)
    {
        if ((value.touchPoint[i].state & OpticalReportTouchPointStateFlag_IsValid) == 0)
        {
            continue;
        }
        input_mt_slot(device->input_dev, i);
        if ((value.touchPoint[i].state & OpticalReportTouchPointStateFlag_IsTouched) != 0)
        {
            input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(device->input_dev, ABS_MT_TOUCH_MAJOR, value.touchPoint[i].width);
            input_report_abs(device->input_dev, ABS_MT_TOUCH_MINOR, value.touchPoint[i].height);
            input_report_abs(device->input_dev, ABS_MT_POSITION_X, value.touchPoint[i].x);
            input_report_abs(device->input_dev, ABS_MT_POSITION_Y, value.touchPoint[i].y);
        }
        else
        {
            input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
        }
    }
    input_sync(device->input_dev);
    return sizeof(value);
}
static long sync_keyboard(device_context *device, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_diagnosis(device_context *device, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_rawtouch(device_context *device, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_touch(device_context *device, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_virtualkey(device_context *device, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long optical_unlocked_ioctl(struct file * filp, unsigned int ctl_code, unsigned long ctl_param)
{
    device_context *device;

    device = filp->private_data;
    if (device == NULL)
    {
        return -EFAULT;
    }

    switch (ctl_code & OPTICAL_IOCTL_CODE_TYPE_MASK)
    {
    case OPTICAL_IOCTL_CODE_TYPE_SET_REPORT:
        return set_report(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_GET_REPORT:
        return get_report(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_ABSOLUTEMOUSE:
        return sync_absolute_mouse(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_SINGLETOUCH:
        return sync_singletouch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_MULTITOUCH:
        return sync_multitouch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_KEYBOARD:
        return sync_keyboard(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_DIAGNOSIS:
        return sync_diagnosis(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_RAWTOUCH:
        return sync_rawtouch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_TOUCH:
        return sync_touch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_VIRTUALKEY:
        return sync_virtualkey(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    }
    return 0;
}

static int optical_open(struct inode * inode, struct file * filp)
{
    device_context* device;
    struct usb_interface* interface;
    int subminor;

    subminor = iminor(inode);

    interface = usb_find_interface(&optical_driver, subminor);

    if (interface == NULL)
    {
        err("%s: interface ptr is NULL.", __func__);
        return -1;
    }
    device = usb_get_intfdata(interface);
    if (device->file_private_data != NULL)
    {
        return -EFAULT;
    }
    device->file_private_data = &filp->private_data;
    filp->private_data = device;

    return 0;
}

static int optical_release(struct inode * inode, struct file * filp)
{
    device_context* device;

    device = filp->private_data;
    if (device != NULL)
    {
        device->file_private_data = NULL;
    }
    filp->private_data = NULL;

    return 0;
}

static struct file_operations optical_fops =
{
    .owner = THIS_MODULE,
    .read = optical_read,
    .write = optical_write,
    .unlocked_ioctl = optical_unlocked_ioctl,
    .open = optical_open,
    .release = optical_release,
};

static void on_interrupt(struct urb* interrupt_urb)
{
    device_context* device;

    device = interrupt_urb->context;

    switch (interrupt_urb->status)
    {
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
        return;
    }

    spin_lock(&device->lock);
    if (interrupt_urb->status == 0)
    {
        if (interrupt_urb->actual_length > 0)
        {
            memcpy(device->buffer, device->ongoing_buffer, interrupt_urb->actual_length);
            device->buffer_length = interrupt_urb->actual_length;
        }
    }
    spin_unlock(&device->lock);

    submit_urb(device);
}

static int optical_open_device(struct input_dev * input_dev)
{
    device_context* device;

    device = input_get_drvdata(input_dev);
    info("%s", __func__);

    submit_urb(device);
    return 0;
}

static void optical_close_device(struct input_dev * input_dev)
{
    device_context* device;

    device = input_get_drvdata(input_dev);
    info("%s", __func__);

    cancel_urb(device);
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
            obj->pipe_interval = intf->cur_altsetting->endpoint[i].desc.bInterval;
            return;
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

    //seems useless
    //input_set_drvdata(obj, device);

    obj->open = optical_open_device;
    obj->close = optical_close_device;

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
    input_mt_init_slots(obj, OPTICAL_TOUCH_POINT_COUNT, INPUT_MT_DIRECT);
}

static int optical_probe(struct usb_interface * intf, const struct usb_device_id *id)
{
    int retval;
    device_context * device;

    do
    {
        device = kzalloc(sizeof(device_context), GFP_KERNEL);
        device->file_private_data = NULL;
        if (device == NULL)
        {
            err("%s: Out of memory.", __func__);
            break;
        }
        do
        {
            device_context_init(device, intf);
            device->input_dev = input_allocate_device();
            if (device->input_dev == NULL)
            {
                break;
            }
            do
            {
                spin_lock_init(&device->lock);
                device->ongoing_buffer = usb_alloc_coherent(device->usb_device, sizeof(device->buffer), GFP_ATOMIC, &device->ongoing_buffer_dma);
                if (device->ongoing_buffer == NULL)
                {
                    break;
                }
                do
                {
                    device->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);
                    if (device->interrupt_urb == NULL)
                    {
                        break;
                    }
                    do
                    {
                        usb_fill_int_urb(device->interrupt_urb, device->usb_device, device->pipe_input, device->ongoing_buffer, sizeof(device->buffer), on_interrupt, device, device->pipe_interval);
                        device->interrupt_urb->transfer_dma = device->ongoing_buffer_dma;
                        device->interrupt_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
                        device->buffer_length = 0;
                        device->interrupt_urb->dev = device->usb_device;
                        input_dev_init(device->input_dev, &device->pool, device->usb_device, &intf->dev);
                        input_set_drvdata(device->input_dev, device);
                        retval = input_register_device(device->input_dev);
                        if (retval != 0)
                        {
                            break;
                        }
                        do
                        {
                            usb_set_intfdata(intf, device);
                            do
                            {
                                msleep(500);
                                if (usb_register_dev(intf, &optical_class) != 0)
                                {
                                    break;
                                }
                                return 0;
                            } while (false);
                            usb_set_intfdata(intf, NULL);
                        } while (false);
                        input_unregister_device(device->input_dev);
                    } while (false);
                    usb_free_urb(device->interrupt_urb);
                } while (false);
                usb_free_coherent(device->usb_device, sizeof(device->buffer), device->ongoing_buffer, device->ongoing_buffer_dma);
            } while (false);
            input_free_device(device->input_dev);
        } while (false);
        if (device->file_private_data != NULL)
        {
            *(device->file_private_data) = NULL;
        }
        device->file_private_data = NULL;
        kfree(device);
    } while (false);
    return -ENOMEM;
}

static void optical_disconnect(struct usb_interface * intf)
{
    device_context* device = usb_get_intfdata(intf);
    int minor;

    minor = intf->minor;
    device = usb_get_intfdata(intf);

    usb_deregister_dev(intf, &optical_class);
    usb_set_intfdata(intf, NULL);
    input_unregister_device(device->input_dev);
    usb_free_urb(device->interrupt_urb);
    usb_free_coherent(device->usb_device, sizeof(device->buffer), device->ongoing_buffer, device->ongoing_buffer_dma);
    input_free_device(device->input_dev);
    if (device->file_private_data != NULL)
    {
        (*device->file_private_data) = NULL;
    }
    device->file_private_data = NULL;
    kfree(device);
}

static struct usb_driver optical_driver =
{
    .name = DRIVER_NAME,
    .probe = optical_probe,
    .disconnect = optical_disconnect,
    .id_table = dev_table,
};


module_usb_driver(optical_driver);

MODULE_DESCRIPTION("USB driver for IRTOUCH optical");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("KOGA");

// necessary ?
MODULE_DEVICE_TABLE(usb, dev_table);

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/input/mt.h>

#include "OpticalDrv.h"

#define DRIVER_NAME     "Optical touch device"

#define err(format, arg...)                \
    printk(KERN_ERR KBUILD_MODNAME ": " format "\n", ##arg)
#define info(format, arg...)                \
    printk(KERN_INFO KBUILD_MODNAME ": " format "\n", ##arg)

#define OPTICAL_MINOR_BASE    0

typedef struct _device_context_pool
{
    char name[128];
    char class_name[32];
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
    const struct optical_variant *variant;
    struct usb_class_driver class;

    struct urb* interrupt_urb;

    spinlock_t lock;

    unsigned char *ongoing_buffer;
    dma_addr_t ongoing_buffer_dma;

    unsigned int max_packet_size;
    unsigned int report_packet_size;
    unsigned int buffer_capacity;
    unsigned char *buffer; //OTD: 10 × 9 + 2 = 92
    unsigned int buffer_length;

    bool urb_submitted;
    bool input_device_open;
    bool disconnected;

    device_context_pool pool;
}
device_context;


static const struct optical_variant variant_touch2 = {
    .device_node_format = "IRTouchOptical%03d",
    .touch_point_count = 2,
};

static const struct optical_variant variant_touch4 = {
    .device_node_format = "OtdUsbRaw%03d",
    .touch_point_count = 10,
};

static struct usb_device_id const dev_table[] =
{
    { USB_DEVICE(0x6615, 0x0084), .driver_info = (kernel_ulong_t)&variant_touch2 },
    { USB_DEVICE(0x6615, 0x0085), .driver_info = (kernel_ulong_t)&variant_touch2 },
    { USB_DEVICE(0x6615, 0x0086), .driver_info = (kernel_ulong_t)&variant_touch2 },
    { USB_DEVICE(0x6615, 0x0087), .driver_info = (kernel_ulong_t)&variant_touch2 },
    { USB_DEVICE(0x6615, 0x0088), .driver_info = (kernel_ulong_t)&variant_touch2 },
    { USB_DEVICE(0x6615, 0x0c20), .driver_info = (kernel_ulong_t)&variant_touch2 },
    { USB_DEVICE(0x2621, 0x2201), .driver_info = (kernel_ulong_t)&variant_touch4 },
    { USB_DEVICE(0x2621, 0x4501), .driver_info = (kernel_ulong_t)&variant_touch4 },
    {}
};

static struct usb_driver optical_driver;
static struct file_operations optical_fops;
static DEFINE_MUTEX(optical_file_lock);

static int submit_urb(device_context* optical, gfp_t gfp_mask)
{
    int retval;

    retval = usb_submit_urb(optical->interrupt_urb, gfp_mask);
    if (retval != 0)
    {
        err("%s: usb_submit_urb failed: %d", __func__, retval);
    }
    return retval;
}
static void cancel_urb(device_context* device)
{
    usb_kill_urb(device->interrupt_urb);
}

static bool optical_has_active_consumer(device_context* optical)
{
    return optical->file_private_data != NULL || optical->input_device_open;
}

static int optical_ensure_urb_started(device_context* optical, gfp_t gfp_mask)
{
    int retval;

    mutex_lock(&optical_file_lock);
    if (optical->disconnected)
    {
        mutex_unlock(&optical_file_lock);
        return -ENODEV;
    }
    if (optical->urb_submitted)
    {
        mutex_unlock(&optical_file_lock);
        return 0;
    }
    retval = submit_urb(optical, gfp_mask);
    optical->urb_submitted = retval == 0;
    mutex_unlock(&optical_file_lock);
    return retval;
}

static void optical_maybe_stop_urb(device_context* optical)
{
    mutex_lock(&optical_file_lock);
    if (!optical->disconnected && optical->urb_submitted &&
        !optical_has_active_consumer(optical))
    {
        optical->urb_submitted = false;
        cancel_urb(optical);
    }
    mutex_unlock(&optical_file_lock);
}

static ssize_t optical_read(struct file * filp, char * buffer, size_t count, loff_t * ppos)
{
    ssize_t r;
    device_context * optical;

    optical = filp->private_data;
    if (optical == NULL)
    {
        return -EFAULT;
    }

    spin_lock_irq(&optical->lock);
    do
    {
        if (optical->buffer_length <= 0)
        {
            r = 0;
            break;
        }
        if (count > optical->buffer_length)
        {
            count = optical->buffer_length;
        }
        optical->buffer_length = 0;
        if (copy_to_user(buffer, optical->buffer, count) != 0)
        {
            r = -EFAULT;
            break;
        }
        r = count;
    } while (false);
    spin_unlock_irq(&optical->lock);

    return r;
}

static ssize_t optical_write(struct file * filp, const char * user_buffer, size_t count, loff_t * ppos)
{
    device_context *optical;

    optical = filp->private_data;
    if (optical == NULL)
    {
        return -EFAULT;
    }

    return -EFAULT;
}

static long set_report(device_context *optical, unsigned short length, void const* data)
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
        r = copy_from_user(kernel_data, data, length);
        if (r != 0)
        {
            break;
        }
        if (length < 1)
        {
            break;
        }
        r = usb_control_msg(optical->usb_device, usb_sndctrlpipe(optical->usb_device, 0), 0, 0x40, 0, 0, kernel_data, length, 1000);
        kfree(kernel_data);
        return r;
    } while (false);
    kfree(kernel_data);
    return -EFAULT;
}
static long get_report(device_context *optical, unsigned short length, void* data)
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
        r = usb_control_msg(optical->usb_device, usb_rcvctrlpipe(optical->usb_device, 0), 0, 0xc0, 0, 0, kernel_data, length, 1000);
        if (r >= 0)
        {
            if (copy_to_user(data, kernel_data, r) != 0)
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
static long sync_absolute_mouse(device_context *optical, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_singletouch(device_context *optical, unsigned short length, void const* data)
{
    OpticalReportPacketSingleTouch value;
    int r;

    if (length < sizeof(value))
    {
        return 0;
    }
    r = copy_from_user(&value, data, sizeof(value));
    if (r != 0)
    {
        return 0;
    }
    if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsValid) == 0)
    {
        return sizeof(value);
    }
    input_mt_slot(optical->input_dev, 0);
    if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsTouched) != 0)
    {
        input_mt_report_slot_state(optical->input_dev, MT_TOOL_FINGER, true);
        input_report_abs(optical->input_dev, ABS_MT_TOUCH_MAJOR, value.touchPoint.width);
        input_report_abs(optical->input_dev, ABS_MT_TOUCH_MINOR, value.touchPoint.height);
        input_report_abs(optical->input_dev, ABS_MT_POSITION_X, value.touchPoint.x);
        input_report_abs(optical->input_dev, ABS_MT_POSITION_Y, value.touchPoint.y);
    }
    else
    {
        input_mt_report_slot_state(optical->input_dev, MT_TOOL_FINGER, false);
    }
    input_sync(optical->input_dev);
    return sizeof(value);
}
static long sync_multitouch(device_context *optical, unsigned short length, void const* data)
{
    OpticalReportTouchPoint *touch_points;
    unsigned int point_count;
    unsigned int packet_size;
    int i;
    int r;

    /* Reports may carry fewer points than the variant maximum (partial
     * update); unreported slots keep their state. */
    if (length < OPTICAL_MULTITOUCH_PACKET_SIZE(1))
    {
        return 0;
    }
    point_count = (length - sizeof(unsigned short)) / sizeof(OpticalReportTouchPoint);
    point_count = min(point_count, optical->variant->touch_point_count);
    packet_size = OPTICAL_MULTITOUCH_PACKET_SIZE(point_count);
    touch_points = kmalloc(packet_size, GFP_KERNEL);
    if (touch_points == NULL)
    {
        return -ENOMEM;
    }
    r = copy_from_user(touch_points, data, packet_size);
    if (r != 0)
    {
        kfree(touch_points);
        return 0;
    }
    for (i = 0; i < point_count; i++)
    {
        /* Ensure we always select the slot so we can report releases even when
         * the incoming report marks the slot as invalid (IsValid == 0).
         * Some upstream code may mark a point invalid instead of explicitly
         * sending an "Up" event; treating invalid as release prevents stuck
         * touches in the input layer. */
        input_mt_slot(optical->input_dev, i);
        if ((touch_points[i].state & OpticalReportTouchPointStateFlag_IsValid) == 0)
        {
            /* Report slot as released */
            input_mt_report_slot_state(optical->input_dev, MT_TOOL_FINGER, false);
            continue;
        }

        if ((touch_points[i].state & OpticalReportTouchPointStateFlag_IsTouched) != 0)
        {
            input_mt_report_slot_state(optical->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(optical->input_dev, ABS_MT_TOUCH_MAJOR, touch_points[i].width);
            input_report_abs(optical->input_dev, ABS_MT_TOUCH_MINOR, touch_points[i].height);
            input_report_abs(optical->input_dev, ABS_MT_POSITION_X, touch_points[i].x);
            input_report_abs(optical->input_dev, ABS_MT_POSITION_Y, touch_points[i].y);
        }
        else
        {
            input_mt_report_slot_state(optical->input_dev, MT_TOOL_FINGER, false);
        }
    }
    input_sync(optical->input_dev);
    kfree(touch_points);
    return packet_size;
}
static long sync_keyboard(device_context *optical, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_diagnosis(device_context *optical, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_rawtouch(device_context *optical, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_touch(device_context *optical, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long sync_virtualkey(device_context *optical, unsigned short length, void const* data)
{
    // TODO
    return 0;
}
static long optical_unlocked_ioctl(struct file * filp, unsigned int ctl_code, unsigned long ctl_param)
{
    device_context *optical;

    optical = filp->private_data;
    if (optical == NULL)
    {
        return -EFAULT;
    }

    switch (ctl_code & OPTICAL_IOCTL_CODE_TYPE_MASK)
    {
    case OPTICAL_IOCTL_CODE_TYPE_SET_REPORT:
        return set_report(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_GET_REPORT:
        return get_report(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_ABSOLUTEMOUSE:
        return sync_absolute_mouse(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_SINGLETOUCH:
        return sync_singletouch(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_MULTITOUCH:
        return sync_multitouch(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_KEYBOARD:
        return sync_keyboard(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_DIAGNOSIS:
        return sync_diagnosis(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_RAWTOUCH:
        return sync_rawtouch(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_TOUCH:
        return sync_touch(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);
    case OPTICAL_IOCTL_CODE_TYPE_SYNC_VIRTUALKEY:
        return sync_virtualkey(optical, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK, (void const*)ctl_param);

    }
    return 0;
}

static int optical_open(struct inode * inode, struct file * filp)
{
    device_context* optical;
    struct usb_interface* interface;
    int retval;
    int subminor;

    subminor = iminor(inode);

    interface = usb_find_interface(&optical_driver, subminor);

    if (interface == NULL)
    {
        err("%s: interface ptr is NULL.", __func__);
        return -1;
    }
    optical = usb_get_intfdata(interface);
    if (optical == NULL)
    {
        return -ENODEV;
    }

    mutex_lock(&optical_file_lock);
    if (optical->file_private_data != NULL)
    {
        mutex_unlock(&optical_file_lock);
        return -EFAULT;
    }
    optical->file_private_data = &filp->private_data;
    filp->private_data = optical;
    mutex_unlock(&optical_file_lock);

    retval = optical_ensure_urb_started(optical, GFP_KERNEL);
    if (retval == 0)
    {
        return 0;
    }

    mutex_lock(&optical_file_lock);
    if (optical->file_private_data == &filp->private_data)
    {
        optical->file_private_data = NULL;
    }
    filp->private_data = NULL;
    mutex_unlock(&optical_file_lock);
    return retval;
}

static int optical_release(struct inode * inode, struct file * filp)
{
    device_context* device;

    mutex_lock(&optical_file_lock);
    device = filp->private_data;
    if (device != NULL)
    {
        device->file_private_data = NULL;
    }
    filp->private_data = NULL;
    mutex_unlock(&optical_file_lock);

    if (device != NULL)
    {
        optical_maybe_stop_urb(device);
    }

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
    device_context* optical;

    optical = interrupt_urb->context;

    switch (interrupt_urb->status)
    {
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
        return;
    }

    spin_lock(&optical->lock);
    if (interrupt_urb->status == 0)
    {
        if (interrupt_urb->actual_length > 0)
        {
            unsigned int length;

            length = min_t(unsigned int, interrupt_urb->actual_length,
                           optical->buffer_capacity);
            memcpy(optical->buffer, optical->ongoing_buffer, length);
            optical->buffer_length = length;
        }
    }
    spin_unlock(&optical->lock);

    /* Flag reads are lockless: worst case is one extra URB cycle before a
     * stop takes effect, which is harmless. A mutex cannot be used here
     * because this is interrupt context. */
    if (optical->disconnected || !optical->urb_submitted ||
        !optical_has_active_consumer(optical))
    {
        return;
    }

    if (submit_urb(optical, GFP_ATOMIC) != 0)
    {
        optical->urb_submitted = false;
    }
}

static int optical_open_device(struct input_dev * input_dev)
{
    device_context* optical;

    optical = input_get_drvdata(input_dev);
    info("%s", __func__);

    mutex_lock(&optical_file_lock);
    optical->input_device_open = true;
    mutex_unlock(&optical_file_lock);

    return optical_ensure_urb_started(optical, GFP_KERNEL);
}

static void optical_close_device(struct input_dev * input_dev)
{
    device_context* device;

    device = input_get_drvdata(input_dev);
    info("%s", __func__);

    mutex_lock(&optical_file_lock);
    device->input_device_open = false;
    mutex_unlock(&optical_file_lock);

    optical_maybe_stop_urb(device);
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
            obj->max_packet_size = usb_endpoint_maxp(&intf->cur_altsetting->endpoint[i].desc);
            obj->pipe_interval = intf->cur_altsetting->endpoint[i].desc.bInterval;
            return;
        }
    }
}

static void input_dev_init(struct input_dev* obj, device_context* optical,
                           struct usb_device* usb_device, struct device* parent)
{
    device_context_pool* pool = &optical->pool;
    if (usb_device->manufacturer != NULL)
    {
        strscpy(pool->name, usb_device->manufacturer, sizeof(pool->name));
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

    //û����ʵ����;
    //input_set_drvdata(obj, optical);

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
    input_mt_init_slots(obj, optical->variant->touch_point_count, INPUT_MT_DIRECT);
}

static int optical_probe(struct usb_interface * intf, const struct usb_device_id *id)
{
    int retval;
    device_context * optical;

    do
    {
        optical = kzalloc(sizeof(device_context), GFP_KERNEL);
        if (optical == NULL)
        {
            err("%s: Out of memory.", __func__);
            break;
        }
        do
        {
            optical->variant = (const struct optical_variant *)id->driver_info;
            snprintf(optical->pool.class_name, sizeof(optical->pool.class_name),
                     "%s", optical->variant->device_node_format);
            optical->class.name = optical->pool.class_name;
            optical->class.fops = &optical_fops;
            optical->class.minor_base = OPTICAL_MINOR_BASE;
            optical->file_private_data = NULL;
            device_context_init(optical, intf);
            optical->report_packet_size =
                OPTICAL_MULTITOUCH_PACKET_SIZE(optical->variant->touch_point_count);
            optical->buffer_capacity = max(optical->max_packet_size,
                                           optical->report_packet_size);
            optical->input_dev = input_allocate_device();
            if (optical->input_dev == NULL)
            {
                break;
            }
            optical->buffer = kzalloc(optical->buffer_capacity, GFP_KERNEL);
            if (optical->buffer == NULL)
            {
                break;
            }
            do
            {
                spin_lock_init(&optical->lock);
                optical->ongoing_buffer = usb_alloc_coherent(optical->usb_device, optical->buffer_capacity, GFP_ATOMIC, &optical->ongoing_buffer_dma);
                if (optical->ongoing_buffer == NULL)
                {
                    break;
                }
                do
                {
                    optical->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);
                    if (optical->interrupt_urb == NULL)
                    {
                        break;
                    }
                    do
                    {
                        usb_fill_int_urb(optical->interrupt_urb, optical->usb_device, optical->pipe_input, optical->ongoing_buffer, optical->buffer_capacity, on_interrupt, optical, optical->pipe_interval);
                        optical->interrupt_urb->transfer_dma = optical->ongoing_buffer_dma;
                        optical->interrupt_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
                        optical->buffer_length = 0;
                        optical->interrupt_urb->dev = optical->usb_device;
                        input_dev_init(optical->input_dev, optical, optical->usb_device, &intf->dev);
                        input_set_drvdata(optical->input_dev, optical);
                        retval = input_register_device(optical->input_dev);
                        if (retval != 0)
                        {
                            break;
                        }
                        do
                        {
                            usb_set_intfdata(intf, optical);
                            do
                            {
                                msleep(500);
                                if (usb_register_dev(intf, &optical->class) != 0)
                                {
                                    break;
                                }
                                return 0;


                            } while (false);
                            usb_set_intfdata(intf, NULL);
                        } while (false);
                        //ԭ��û�е���input_unregister_device
                        input_unregister_device(optical->input_dev);
                        optical->input_dev = NULL;
                    } while (false);
                    usb_free_urb(optical->interrupt_urb);
                } while (false);
                usb_free_coherent(optical->usb_device, optical->buffer_capacity, optical->ongoing_buffer, optical->ongoing_buffer_dma);
            } while (false);
            kfree(optical->buffer);
            if (optical->input_dev != NULL)
            {
                input_free_device(optical->input_dev);
            }
        } while (false);
        if (optical->file_private_data != NULL)
        {
            *(optical->file_private_data) = NULL;
        }
        optical->file_private_data = NULL;
        kfree(optical);
    } while (false);
    return -ENOMEM;
}

static void optical_disconnect(struct usb_interface * intf)
{
    device_context* optical = usb_get_intfdata(intf);

    usb_deregister_dev(intf, &optical->class);
    usb_set_intfdata(intf, NULL);

    mutex_lock(&optical_file_lock);
    optical->disconnected = true;
    optical->urb_submitted = false;
    optical->input_device_open = false;
    if (optical->file_private_data != NULL)
    {
        (*optical->file_private_data) = NULL;
    }
    optical->file_private_data = NULL;
    mutex_unlock(&optical_file_lock);

    cancel_urb(optical);
    input_unregister_device(optical->input_dev);
    usb_free_urb(optical->interrupt_urb);
    usb_free_coherent(optical->usb_device, optical->buffer_capacity, optical->ongoing_buffer, optical->ongoing_buffer_dma);
    kfree(optical->buffer);
    kfree(optical);
}

static struct usb_driver optical_driver =
{
    .name = DRIVER_NAME,
    .probe = optical_probe,
    .disconnect = optical_disconnect,
    .id_table = dev_table,
};


module_usb_driver(optical_driver);

MODULE_DESCRIPTION("USB driver for Optical touch screen");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Optical touch screen");

// necessary ?
MODULE_DEVICE_TABLE(usb, dev_table);

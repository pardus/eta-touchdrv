#include <linux/errno.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/usb/input.h>

#ifndef OPTICAL_TOUCH_POINT_COUNT
#define OPTICAL_TOUCH_POINT_COUNT 10
#endif

#include <OpticalDrv.h>

#define DRIVER_NAME "Optical touch device"

#define err(format, arg...) printk(KERN_ERR KBUILD_MODNAME ": " format "\n", ##arg)

static struct usb_device_id const dev_table[] = {
    {USB_DEVICE(0x2621, 0x2201)},
    {USB_DEVICE(0x2621, 0x4501)},
    {},
};

static void optical_delete(struct kref* kref) {
    device_context* device = container_of(kref, device_context, kref);

    kfree(device);
}

static void optical_report_frame(device_context* device, unsigned char const* data, size_t length) {
    OpticalReportPacketMultiTouch const* packet = (OpticalReportPacketMultiTouch const*)data;
    int i;

    if (length < sizeof(*packet)) {
        return;
    }

    for (i = 0; i < OPTICAL_TOUCH_POINT_COUNT; i++) {
        input_mt_slot(device->input_dev, i);

        if ((packet->touchPoint[i].state & OpticalReportTouchPointStateFlag_IsValid) == 0) {
            input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
            continue;
        }

        if ((packet->touchPoint[i].state & OpticalReportTouchPointStateFlag_IsTouched) != 0) {
            input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(device->input_dev, ABS_MT_TOUCH_MAJOR, packet->touchPoint[i].width);
            input_report_abs(device->input_dev, ABS_MT_TOUCH_MINOR, packet->touchPoint[i].height);
            input_report_abs(device->input_dev, ABS_MT_POSITION_X, packet->touchPoint[i].x);
            input_report_abs(device->input_dev, ABS_MT_POSITION_Y, packet->touchPoint[i].y);
        } else {
            input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
        }
    }

    input_mt_sync_frame(device->input_dev);
    input_sync(device->input_dev);
}

static int optical_submit_urb(device_context* device, gfp_t flags) {
    int retval;

    mutex_lock(&device->io_lock);
    if (device->disconnected) {
        mutex_unlock(&device->io_lock);
        return -ENODEV;
    }

    usb_anchor_urb(device->interrupt_urb, &device->submitted);
    retval = usb_submit_urb(device->interrupt_urb, flags);
    if (retval != 0) {
        usb_unanchor_urb(device->interrupt_urb);
    }
    mutex_unlock(&device->io_lock);

    return retval;
}

static void optical_stop_io(device_context* device) {
    mutex_lock(&device->io_lock);
    device->disconnected = true;
    mutex_unlock(&device->io_lock);

    usb_kill_anchored_urbs(&device->submitted);
}

static void on_interrupt(struct urb* interrupt_urb) {
    device_context* device = interrupt_urb->context;

    switch (interrupt_urb->status) {
        case 0:
            if (interrupt_urb->actual_length > 0) {
                optical_report_frame(device, device->ongoing_buffer, interrupt_urb->actual_length);
            }
            break;
        case -ECONNRESET:
        case -ENOENT:
        case -ESHUTDOWN:
            return;
        default:
            break;
    }

    if (optical_submit_urb(device, GFP_ATOMIC) != 0 && !device->disconnected) {
        err("%s: failed to resubmit interrupt urb", __func__);
    }
}

static int optical_open_device(struct input_dev* input_dev) {
    device_context* device = input_get_drvdata(input_dev);

    return optical_submit_urb(device, GFP_KERNEL);
}

static void optical_close_device(struct input_dev* input_dev) {
    device_context* device = input_get_drvdata(input_dev);

    usb_kill_anchored_urbs(&device->submitted);
}

static void device_context_init(device_context* device, struct usb_interface* intf) {
    int i;

    device->usb_device = interface_to_usbdev(intf);

    for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
        if (intf->cur_altsetting->endpoint[i].desc.bEndpointAddress & USB_DIR_IN) {
            device->pipe_input = usb_rcvintpipe(
                device->usb_device, intf->cur_altsetting->endpoint[i].desc.bEndpointAddress);
            device->pipe_interval = intf->cur_altsetting->endpoint[i].desc.bInterval;
            return;
        }
    }
}

static int input_dev_init(struct input_dev* input_dev, device_context_pool* pool,
                          struct usb_device* usb_device, struct device* parent) {
    int retval;

    if (usb_device->manufacturer != NULL) {
        strscpy(pool->name, usb_device->manufacturer, sizeof(pool->name));
    } else {
        pool->name[0] = 0;
    }

    if (usb_device->product != NULL) {
        strlcat(pool->name, " ", sizeof(pool->name));
        strlcat(pool->name, usb_device->product, sizeof(pool->name));
    }

    if (strlen(pool->name) == 0) {
        snprintf(pool->name, sizeof(pool->name), "Optical touch device %04x:%04x",
                 le16_to_cpu(usb_device->descriptor.idVendor),
                 le16_to_cpu(usb_device->descriptor.idProduct));
    }

    usb_make_path(usb_device, pool->phys, sizeof(pool->phys));
    strlcat(pool->phys, "/input0", sizeof(pool->phys));

    input_dev->name = pool->name;
    input_dev->phys = pool->phys;

    usb_to_input_id(usb_device, &input_dev->id);
    input_dev->dev.parent = parent;

    input_dev->open = optical_open_device;
    input_dev->close = optical_close_device;

    input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
    set_bit(BTN_TOUCH, input_dev->keybit);
    set_bit(EV_SYN, input_dev->evbit);
    set_bit(EV_KEY, input_dev->evbit);
    set_bit(EV_ABS, input_dev->evbit);
    input_dev->absbit[0] = BIT(ABS_MT_PRESSURE) | BIT(ABS_MT_POSITION_X) | BIT(ABS_MT_POSITION_Y) |
                           BIT(ABS_MT_TOUCH_MAJOR) | BIT(ABS_MT_TOUCH_MINOR);

    input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 1, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, 32767, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, 32767, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 32767, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR, 0, 32767, 0, 0);

    retval = input_mt_init_slots(input_dev, OPTICAL_TOUCH_POINT_COUNT, INPUT_MT_DIRECT);
    if (retval != 0) {
        return retval;
    }

    return 0;
}

static int optical_probe(struct usb_interface* intf, const struct usb_device_id* id) {
    int retval;
    device_context* device;

    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (device == NULL) {
        err("%s: out of memory", __func__);
        return -ENOMEM;
    }

    kref_init(&device->kref);
    mutex_init(&device->io_lock);
    init_usb_anchor(&device->submitted);
    device_context_init(device, intf);

    device->input_dev = input_allocate_device();
    if (device->input_dev == NULL) {
        retval = -ENOMEM;
        goto err_put;
    }

    retval = input_dev_init(device->input_dev, &device->pool, device->usb_device, &intf->dev);
    if (retval != 0) {
        goto err_free_input;
    }

    device->ongoing_buffer =
        usb_alloc_coherent(device->usb_device, sizeof(OpticalReportPacketMultiTouch), GFP_KERNEL,
                           &device->ongoing_buffer_dma);
    if (device->ongoing_buffer == NULL) {
        retval = -ENOMEM;
        goto err_free_input;
    }

    device->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (device->interrupt_urb == NULL) {
        retval = -ENOMEM;
        goto err_free_buffer;
    }

    usb_fill_int_urb(device->interrupt_urb, device->usb_device, device->pipe_input,
                     device->ongoing_buffer, sizeof(OpticalReportPacketMultiTouch), on_interrupt,
                     device, device->pipe_interval);
    device->interrupt_urb->transfer_dma = device->ongoing_buffer_dma;
    device->interrupt_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    input_set_drvdata(device->input_dev, device);

    retval = input_register_device(device->input_dev);
    if (retval != 0) {
        goto err_free_urb;
    }

    usb_set_intfdata(intf, device);
    return 0;

err_free_urb:
    usb_free_urb(device->interrupt_urb);
err_free_buffer:
    usb_free_coherent(device->usb_device, sizeof(OpticalReportPacketMultiTouch),
                      device->ongoing_buffer, device->ongoing_buffer_dma);
err_free_input:
    input_free_device(device->input_dev);
err_put:
    kref_put(&device->kref, optical_delete);
    return retval;
}

static void optical_disconnect(struct usb_interface* intf) {
    device_context* device = usb_get_intfdata(intf);

    if (device == NULL) {
        return;
    }

    usb_set_intfdata(intf, NULL);
    optical_stop_io(device);
    input_unregister_device(device->input_dev);
    device->input_dev = NULL;
    usb_free_urb(device->interrupt_urb);
    usb_free_coherent(device->usb_device, sizeof(OpticalReportPacketMultiTouch),
                      device->ongoing_buffer, device->ongoing_buffer_dma);
    kref_put(&device->kref, optical_delete);
}

static struct usb_driver optical_driver = {
    .name = DRIVER_NAME,
    .probe = optical_probe,
    .disconnect = optical_disconnect,
    .id_table = dev_table,
};

module_usb_driver(optical_driver);

MODULE_DESCRIPTION("USB driver for Optical touch screen");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Optical touch screen");
MODULE_DEVICE_TABLE(usb, dev_table);

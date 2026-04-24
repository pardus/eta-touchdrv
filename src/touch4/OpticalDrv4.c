#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>

#define DEVICE_NODE_FORMAT "OpticalUsbRaw%03d"
#define OPTICAL_TOUCH_POINT_COUNT 10

#include <OpticalDrv.h>

#define DRIVER_NAME "Optical touch device"

#define err(format, arg...)                                                    \
  printk(KERN_ERR KBUILD_MODNAME ": " format "\n", ##arg)
#define info(format, arg...)                                                   \
  printk(KERN_INFO KBUILD_MODNAME ": " format "\n", ##arg)

#define OPTICAL_MINOR_BASE 0


static struct usb_device_id const dev_table[] = {
    {USB_DEVICE(0x2621, 0x2201)}, {USB_DEVICE(0x2621, 0x4501)}, {}};

static struct file_operations optical_fops;
static struct usb_driver optical_driver;
static struct usb_class_driver optical_class = {
    .name = DEVICE_NODE_FORMAT,
    .fops = &optical_fops,
    .minor_base = OPTICAL_MINOR_BASE,
};

static void submit_urb(device_context *otd) {
  int retval;

  retval = usb_submit_urb(otd->interrupt_urb, GFP_KERNEL);
  if (retval != 0) {
    return;
  }
}
static void cancel_urb(device_context *device) {
  usb_kill_urb(device->interrupt_urb);
}

static ssize_t optical_read(struct file *filp, char *buffer, size_t count,
                            loff_t *ppos) {
  ssize_t r;
  device_context *otd;

  otd = filp->private_data;
  if (otd == NULL) {
    return -EFAULT;
  }

  spin_lock_irq(&otd->lock);
  do {
    if (otd->buffer_length <= 0) {
      r = 0;
      break;
    }
    if (count > otd->buffer_length) {
      count = otd->buffer_length;
    }
    otd->buffer_length = 0;
    if (copy_to_user(buffer, otd->buffer, count) != 0) {
      r = -EFAULT;
      break;
    }
    r = count;
  } while (false);
  spin_unlock_irq(&otd->lock);

  return r;
}

static ssize_t optical_write(struct file *filp, const char *user_buffer,
                             size_t count, loff_t *ppos) {
  device_context *otd;

  otd = filp->private_data;
  if (otd == NULL) {
    return -EFAULT;
  }

  return -EFAULT;
}

static long set_report(device_context *otd, unsigned short length,
                       void const *data) {
  void *kernel_data;
  int r;

  kernel_data = kmalloc(length, GFP_KERNEL);
  if (kernel_data == NULL) {
    return -ENOMEM;
  }
  do {
    r = copy_from_user(kernel_data, data, length);
    if (r != 0) {
      break;
    }
    if (length < 1) {
      break;
    }
    r = usb_control_msg(otd->usb_device, usb_sndctrlpipe(otd->usb_device, 0), 0,
                        0x40, 0, 0, kernel_data, length, 1000);
    kfree(kernel_data);
    return r;
  } while (false);
  kfree(kernel_data);
  return -EFAULT;
}
static long get_report(device_context *otd, unsigned short length, void *data) {
  void *kernel_data;
  int r;

  kernel_data = kmalloc(length, GFP_KERNEL);
  if (kernel_data == NULL) {
    return -ENOMEM;
  }
  do {
    if (length < 1) {
      break;
    }
    r = usb_control_msg(otd->usb_device, usb_rcvctrlpipe(otd->usb_device, 0), 0,
                        0xc0, 0, 0, kernel_data, length, 1000);
    if (r >= 0) {
      if (copy_to_user(data, kernel_data, r) != 0) {
        break;
      }
    }
    kfree(kernel_data);
    return r;
  } while (false);
  kfree(kernel_data);
  return -EFAULT;
}
static long sync_absolute_mouse(device_context *otd, unsigned short length,
                                void const *data) {
  // TODO
  return 0;
}
static long sync_singletouch(device_context *otd, unsigned short length,
                             void const *data) {
  OpticalReportPacketSingleTouch value;
  int r;

  if (length < sizeof(value)) {
    return 0;
  }
  r = copy_from_user(&value, data, sizeof(value));
  if (r != 0) {
    return 0;
  }
  if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsValid) ==
      0) {
    return sizeof(value);
  }
  input_mt_slot(otd->input_dev, 0);
  if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsTouched) !=
      0) {
    input_mt_report_slot_state(otd->input_dev, MT_TOOL_FINGER, true);
    input_report_abs(otd->input_dev, ABS_MT_TOUCH_MAJOR,
                     value.touchPoint.width);
    input_report_abs(otd->input_dev, ABS_MT_TOUCH_MINOR,
                     value.touchPoint.height);
    input_report_abs(otd->input_dev, ABS_MT_POSITION_X, value.touchPoint.x);
    input_report_abs(otd->input_dev, ABS_MT_POSITION_Y, value.touchPoint.y);
  } else {
    input_mt_report_slot_state(otd->input_dev, MT_TOOL_FINGER, false);
  }
  input_sync(otd->input_dev);
  return sizeof(value);
}
static long sync_multitouch(device_context *otd, unsigned short length,
                            void const *data) {
  OpticalReportPacketMultiTouch value;
  int i;
  int r;

  if (length < sizeof(value)) {
    return 0;
  }
  r = copy_from_user(&value, data, sizeof(value));
  if (r != 0) {
    return 0;
  }
  for (i = 0; i < sizeof(value.touchPoint) / sizeof(value.touchPoint[0]); i++) {
    /* Ensure we always select the slot so we can report releases even when
     * the incoming report marks the slot as invalid (IsValid == 0).
     * Some upstream code may mark a point invalid instead of explicitly
     * sending an "Up" event; treating invalid as release prevents stuck
     * touches in the input layer. */
    input_mt_slot(otd->input_dev, i);
    if ((value.touchPoint[i].state &
         OpticalReportTouchPointStateFlag_IsValid) == 0) {
      /* Report slot as released */
      input_mt_report_slot_state(otd->input_dev, MT_TOOL_FINGER, false);
      continue;
    }

    if ((value.touchPoint[i].state &
         OpticalReportTouchPointStateFlag_IsTouched) != 0) {
      input_mt_report_slot_state(otd->input_dev, MT_TOOL_FINGER, true);
      input_report_abs(otd->input_dev, ABS_MT_TOUCH_MAJOR,
                       value.touchPoint[i].width);
      input_report_abs(otd->input_dev, ABS_MT_TOUCH_MINOR,
                       value.touchPoint[i].height);
      input_report_abs(otd->input_dev, ABS_MT_POSITION_X,
                       value.touchPoint[i].x);
      input_report_abs(otd->input_dev, ABS_MT_POSITION_Y,
                       value.touchPoint[i].y);
    } else {
      input_mt_report_slot_state(otd->input_dev, MT_TOOL_FINGER, false);
    }
  }
  input_sync(otd->input_dev);
  return sizeof(value);
}
static long sync_keyboard(device_context *otd, unsigned short length,
                          void const *data) {
  // TODO
  return 0;
}
static long sync_diagnosis(device_context *otd, unsigned short length,
                           void const *data) {
  // TODO
  return 0;
}
static long sync_rawtouch(device_context *otd, unsigned short length,
                          void const *data) {
  // TODO
  return 0;
}
static long sync_touch(device_context *otd, unsigned short length,
                       void const *data) {
  // TODO
  return 0;
}
static long sync_virtualkey(device_context *otd, unsigned short length,
                            void const *data) {
  // TODO
  return 0;
}
static long optical_unlocked_ioctl(struct file *filp, unsigned int ctl_code,
                                   unsigned long ctl_param) {
  device_context *otd;

  otd = filp->private_data;
  if (otd == NULL) {
    return -EFAULT;
  }

  switch (ctl_code & OPTICAL_IOCTL_CODE_TYPE_MASK) {
  case OPTICAL_IOCTL_CODE_TYPE_SET_REPORT:
    return set_report(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                      (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_GET_REPORT:
    return get_report(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                      (void *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_ABSOLUTEMOUSE:
    return sync_absolute_mouse(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                               (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_SINGLETOUCH:
    return sync_singletouch(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                            (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_MULTITOUCH:
    return sync_multitouch(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                           (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_KEYBOARD:
    return sync_keyboard(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                         (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_DIAGNOSIS:
    return sync_diagnosis(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                          (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_RAWTOUCH:
    return sync_rawtouch(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                         (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_TOUCH:
    return sync_touch(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                      (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_VIRTUALKEY:
    return sync_virtualkey(otd, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                           (void const *)ctl_param);
  }
  return 0;
}

static int optical_open(struct inode *inode, struct file *filp) {
  device_context *otd;
  struct usb_interface *interface;
  int subminor;

  subminor = iminor(inode);

  interface = usb_find_interface(&optical_driver, subminor);

  if (interface == NULL) {
    err("%s: interface ptr is NULL.", __func__);
    return -1;
  }
  otd = usb_get_intfdata(interface);
  if (otd->file_private_data != NULL) {
    return -EFAULT;
  }
  otd->file_private_data = &filp->private_data;
  filp->private_data = otd;

  return 0;
}

static int optical_release(struct inode *inode, struct file *filp) {
  device_context *device;

  device = filp->private_data;
  if (device != NULL) {
    device->file_private_data = NULL;
  }
  filp->private_data = NULL;

  return 0;
}

static struct file_operations optical_fops = {
    .owner = THIS_MODULE,
    .read = optical_read,
    .write = optical_write,
    .unlocked_ioctl = optical_unlocked_ioctl,
    .open = optical_open,
    .release = optical_release,
};

static void on_interrupt(struct urb *interrupt_urb) {
  device_context *otd;

  otd = interrupt_urb->context;

  switch (interrupt_urb->status) {
  case -ECONNRESET:
  case -ENOENT:
  case -ESHUTDOWN:
    return;
  }

  spin_lock(&otd->lock);
  if (interrupt_urb->status == 0) {
    if (interrupt_urb->actual_length > 0) {
      memcpy(otd->buffer, otd->ongoing_buffer, interrupt_urb->actual_length);
      otd->buffer_length = interrupt_urb->actual_length;
    }
  }
  spin_unlock(&otd->lock);

  submit_urb(otd);
}

static int optical_open_device(struct input_dev *input_dev) {
  device_context *otd;

  otd = input_get_drvdata(input_dev);
  info("%s", __func__);

  submit_urb(otd);
  return 0;
}

static void optical_close_device(struct input_dev *input_dev) {
  device_context *device;

  device = input_get_drvdata(input_dev);
  info("%s", __func__);

  cancel_urb(device);
}

static void device_context_init(device_context *obj,
                                struct usb_interface *intf) {
  int i;

  obj->usb_device = interface_to_usbdev(intf);

  for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
    if (intf->cur_altsetting->endpoint[i].desc.bEndpointAddress & USB_DIR_IN) {
      obj->pipe_input = usb_rcvintpipe(
          obj->usb_device,
          intf->cur_altsetting->endpoint[i].desc.bEndpointAddress);
      obj->pipe_interval = intf->cur_altsetting->endpoint[i].desc.bInterval;
      return;
    }
  }
}

static void input_dev_init(struct input_dev *obj, device_context_pool *pool,
                           struct usb_device *usb_device,
                           struct device *parent) {
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

  obj->name = pool->name;
  obj->phys = pool->phys;

  usb_to_input_id(usb_device, &obj->id);
  obj->dev.parent = parent;

  // û����ʵ����;
  // input_set_drvdata(obj, otd);

  obj->open = optical_open_device;
  obj->close = optical_close_device;

  obj->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
  set_bit(BTN_TOUCH, obj->keybit);
  set_bit(EV_SYN, obj->evbit);
  set_bit(EV_KEY, obj->evbit);
  set_bit(EV_ABS, obj->evbit);
  obj->absbit[0] = BIT(ABS_MT_PRESSURE) | BIT(ABS_MT_POSITION_X) |
                   BIT(ABS_MT_POSITION_Y) | BIT(ABS_MT_TOUCH_MAJOR) |
                   BIT(ABS_MT_TOUCH_MINOR);

  input_set_abs_params(obj, ABS_MT_PRESSURE, 0, 1, 0, 0);
  input_set_abs_params(obj, ABS_MT_POSITION_X, 0, 32767, 0, 0);
  input_set_abs_params(obj, ABS_MT_POSITION_Y, 0, 32767, 0, 0);
  input_set_abs_params(obj, ABS_MT_TOUCH_MAJOR, 0, 32767, 0, 0);
  input_set_abs_params(obj, ABS_MT_TOUCH_MINOR, 0, 32767, 0, 0);
  input_mt_init_slots(obj, OPTICAL_TOUCH_POINT_COUNT, INPUT_MT_DIRECT);
}

static int optical_probe(struct usb_interface *intf,
                         const struct usb_device_id *id) {
  int retval;
  device_context *otd;

  do {
    otd = kzalloc(sizeof(device_context), GFP_KERNEL);
    otd->file_private_data = NULL;
    if (otd == NULL) {
      err("%s: Out of memory.", __func__);
      break;
    }
    do {
      device_context_init(otd, intf);
      otd->input_dev = input_allocate_device();
      if (otd->input_dev == NULL) {
        break;
      }
      do {
        spin_lock_init(&otd->lock);
        otd->ongoing_buffer =
            usb_alloc_coherent(otd->usb_device, sizeof(otd->buffer), GFP_ATOMIC,
                               &otd->ongoing_buffer_dma);
        if (otd->ongoing_buffer == NULL) {
          break;
        }
        do {
          otd->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);
          if (otd->interrupt_urb == NULL) {
            break;
          }
          do {
            usb_fill_int_urb(otd->interrupt_urb, otd->usb_device,
                             otd->pipe_input, otd->ongoing_buffer,
                             sizeof(otd->buffer), on_interrupt, otd,
                             otd->pipe_interval);
            otd->interrupt_urb->transfer_dma = otd->ongoing_buffer_dma;
            otd->interrupt_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
            otd->buffer_length = 0;
            otd->interrupt_urb->dev = otd->usb_device;
            input_dev_init(otd->input_dev, &otd->pool, otd->usb_device,
                           &intf->dev);
            input_set_drvdata(otd->input_dev, otd);
            retval = input_register_device(otd->input_dev);
            if (retval != 0) {
              break;
            }
            do {
              usb_set_intfdata(intf, otd);
              do {
                msleep(500);
                if (usb_register_dev(intf, &optical_class) != 0) {
                  break;
                }
                return 0;

              } while (false);
              usb_set_intfdata(intf, NULL);
            } while (false);
            // ԭ��û�е���input_unregister_device
            input_unregister_device(otd->input_dev);
          } while (false);
          usb_free_urb(otd->interrupt_urb);
        } while (false);
        usb_free_coherent(otd->usb_device, sizeof(otd->buffer),
                          otd->ongoing_buffer, otd->ongoing_buffer_dma);
      } while (false);
      input_free_device(otd->input_dev);
    } while (false);
    if (otd->file_private_data != NULL) {
      *(otd->file_private_data) = NULL;
    }
    otd->file_private_data = NULL;
    kfree(otd);
  } while (false);
  return -ENOMEM;
}

static void optical_disconnect(struct usb_interface *intf) {
  device_context *otd = usb_get_intfdata(intf);
  int minor;

  minor = intf->minor;
  otd = usb_get_intfdata(intf);

  usb_deregister_dev(intf, &optical_class);
  usb_set_intfdata(intf, NULL);
  input_unregister_device(otd->input_dev);
  usb_free_urb(otd->interrupt_urb);
  usb_free_coherent(otd->usb_device, sizeof(otd->buffer), otd->ongoing_buffer,
                    otd->ongoing_buffer_dma);
  input_free_device(otd->input_dev);
  if (otd->file_private_data != NULL) {
    (*otd->file_private_data) = NULL;
  }
  otd->file_private_data = NULL;
  kfree(otd);
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

// necessary ?
MODULE_DEVICE_TABLE(usb, dev_table);

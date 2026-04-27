#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/version.h>

#define DEVICE_NODE_FORMAT "OtdUsbRaw%03d"
#define OPTICAL_TOUCH_POINT_COUNT 10

#include <OpticalDrv.h>

#define DRIVER_NAME "Optical touch device"

#define err(format, arg...) pr_err(KBUILD_MODNAME ": " format "\n", ##arg)
#define info(format, arg...) pr_info(KBUILD_MODNAME ": " format "\n", ##arg)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0))
#define strlcpy strscpy
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define optical_access_ok(type, ptr, size) access_ok(ptr, size)
#else
#define optical_access_ok(type, ptr, size) access_ok(type, ptr, size)
#endif

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
static DEFINE_MUTEX(optical_file_lock);

static void submit_urb(device_context *device) {
  int retval;

  retval = usb_submit_urb(device->interrupt_urb, GFP_KERNEL);
  if (retval != 0) {
    return;
  }
}
static void cancel_urb(device_context *device) {
  usb_kill_urb(device->interrupt_urb);
}

static void optical_register_work(struct work_struct *work) {
  device_context *device = container_of(to_delayed_work(work), device_context,
                                     register_work);

  if (usb_register_dev(device->intf, &optical_class) != 0) {
    err("%s: usb_register_dev failed", __func__);
    return;
  }
  mutex_lock(&optical_file_lock);
  device->registered = true;
  mutex_unlock(&optical_file_lock);
}

static ssize_t optical_read(struct file *filp, char *buffer, size_t count,
                            loff_t *ppos) {
  ssize_t r;
  unsigned char kernel_buffer[64];
  device_context *device;

  device = filp->private_data;
  if (device == NULL) {
    return -EFAULT;
  }

  r = wait_event_interruptible(device->read_wait,
                               device->buffer_length > 0 || device->disconnected);
  if (r != 0) {
    return r;
  }

  spin_lock_irq(&device->lock);
  do {
    if (device->buffer_length <= 0) {
      r = device->disconnected ? -ENODEV : 0;
      break;
    }
    if (count > device->buffer_length) {
      count = device->buffer_length;
    }
    memcpy(kernel_buffer, device->buffer, count);
    device->buffer_length = 0;
    r = count;
  } while (false);
  spin_unlock_irq(&device->lock);

  if (r > 0 && copy_to_user(buffer, kernel_buffer, r) != 0) {
    return -EFAULT;
  }

  return r;
}

static ssize_t optical_write(struct file *filp, const char *user_buffer,
                             size_t count, loff_t *ppos) {
  device_context *device;

  device = filp->private_data;
  if (device == NULL) {
    return -EFAULT;
  }

  return -EFAULT;
}

static long set_report(device_context *device, unsigned short length,
                       void const *data) {
  void *kernel_data;
  int r;

  if (!optical_access_ok(VERIFY_READ, data, length)) {
    return -EFAULT;
  }
  kernel_data = kzalloc(length, GFP_KERNEL);
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
    r = usb_control_msg(device->usb_device, usb_sndctrlpipe(device->usb_device, 0), 0,
                        0x40, 0, 0, kernel_data, length, 1000);
    kfree(kernel_data);
    return r;
  } while (false);
  kfree(kernel_data);
  return -EFAULT;
}
static long get_report(device_context *device, unsigned short length, void *data) {
  void *kernel_data;
  int r;

  if (!optical_access_ok(VERIFY_WRITE, data, length)) {
    return -EFAULT;
  }
  kernel_data = kzalloc(length, GFP_KERNEL);
  if (kernel_data == NULL) {
    return -ENOMEM;
  }
  do {
    if (length < 1) {
      break;
    }
    r = usb_control_msg(device->usb_device, usb_rcvctrlpipe(device->usb_device, 0), 0,
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
static long sync_absolute_mouse(device_context *device, unsigned short length,
                                void const *data) {
  // TODO
  return 0;
}
static long sync_singletouch(device_context *device, unsigned short length,
                             void const *data) {
  OpticalReportPacketSingleTouch value;
  int r;

  if (length < sizeof(value)) {
    return 0;
  }
  if (!optical_access_ok(VERIFY_READ, data, sizeof(value))) {
    return -EFAULT;
  }
  r = copy_from_user(&value, data, sizeof(value));
  if (r != 0) {
    return 0;
  }
  input_mt_slot(device->input_dev, 0);
  if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsValid) ==
      0) {
    /* Report slot as released */
    input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
    input_sync(device->input_dev);
    return sizeof(value);
  }
  if ((value.touchPoint.state & OpticalReportTouchPointStateFlag_IsTouched) !=
      0) {
    input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, true);
    input_report_abs(device->input_dev, ABS_MT_TOUCH_MAJOR,
                     value.touchPoint.width);
    input_report_abs(device->input_dev, ABS_MT_TOUCH_MINOR,
                     value.touchPoint.height);
    input_report_abs(device->input_dev, ABS_MT_POSITION_X, value.touchPoint.x);
    input_report_abs(device->input_dev, ABS_MT_POSITION_Y, value.touchPoint.y);
  } else {
    input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
  }
  input_sync(device->input_dev);
  return sizeof(value);
}
static long sync_multitouch(device_context *device, unsigned short length,
                            void const *data) {
  OpticalReportPacketMultiTouch value;
  int i;
  int r;

  if (length < sizeof(value)) {
    return 0;
  }
  if (!optical_access_ok(VERIFY_READ, data, sizeof(value))) {
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
    input_mt_slot(device->input_dev, i);
    if ((value.touchPoint[i].state &
         OpticalReportTouchPointStateFlag_IsValid) == 0) {
      /* Report slot as released */
      input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
      continue;
    }

    if ((value.touchPoint[i].state &
         OpticalReportTouchPointStateFlag_IsTouched) != 0) {
      input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, true);
      input_report_abs(device->input_dev, ABS_MT_TOUCH_MAJOR,
                       value.touchPoint[i].width);
      input_report_abs(device->input_dev, ABS_MT_TOUCH_MINOR,
                       value.touchPoint[i].height);
      input_report_abs(device->input_dev, ABS_MT_POSITION_X,
                       value.touchPoint[i].x);
      input_report_abs(device->input_dev, ABS_MT_POSITION_Y,
                       value.touchPoint[i].y);
    } else {
      input_mt_report_slot_state(device->input_dev, MT_TOOL_FINGER, false);
    }
  }
  input_sync(device->input_dev);
  return sizeof(value);
}
static long sync_keyboard(device_context *device, unsigned short length,
                          void const *data) {
  // TODO
  return 0;
}
static long sync_diagnosis(device_context *device, unsigned short length,
                           void const *data) {
  // TODO
  return 0;
}
static long sync_rawtouch(device_context *device, unsigned short length,
                          void const *data) {
  // TODO
  return 0;
}
static long sync_touch(device_context *device, unsigned short length,
                       void const *data) {
  // TODO
  return 0;
}
static long sync_virtualkey(device_context *device, unsigned short length,
                            void const *data) {
  // TODO
  return 0;
}
static long optical_unlocked_ioctl(struct file *filp, unsigned int ctl_code,
                                   unsigned long ctl_param) {
  device_context *device;

  device = filp->private_data;
  if (device == NULL) {
    return -EFAULT;
  }

  switch (ctl_code & OPTICAL_IOCTL_CODE_TYPE_MASK) {
  case OPTICAL_IOCTL_CODE_TYPE_SET_REPORT:
    return set_report(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                      (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_GET_REPORT:
    return get_report(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                      (void *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_ABSOLUTEMOUSE:
    return sync_absolute_mouse(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                               (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_SINGLETOUCH:
    return sync_singletouch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                            (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_MULTITOUCH:
    return sync_multitouch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                           (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_KEYBOARD:
    return sync_keyboard(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                         (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_DIAGNOSIS:
    return sync_diagnosis(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                          (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_RAWTOUCH:
    return sync_rawtouch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                         (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_TOUCH:
    return sync_touch(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                      (void const *)ctl_param);
  case OPTICAL_IOCTL_CODE_TYPE_SYNC_VIRTUALKEY:
    return sync_virtualkey(device, ctl_code & OPTICAL_IOCTL_CODE_LENGTH_MASK,
                           (void const *)ctl_param);
  }
  return 0;
}

static int optical_open(struct inode *inode, struct file *filp) {
  device_context *device;
  struct usb_interface *interface;
  int subminor;

  subminor = iminor(inode);

  interface = usb_find_interface(&optical_driver, subminor);

  if (interface == NULL) {
    err("%s: interface ptr is NULL.", __func__);
    return -ENODEV;
  }
  mutex_lock(&optical_file_lock);
  device = usb_get_intfdata(interface);
  if (device == NULL) {
    mutex_unlock(&optical_file_lock);
    err("%s: device context is NULL.", __func__);
    return -ENODEV;
  }
  if (device->file_private_data != NULL) {
    mutex_unlock(&optical_file_lock);
    return -EBUSY;
  }
  device->file_private_data = &filp->private_data;
  filp->private_data = device;
  mutex_unlock(&optical_file_lock);

  return 0;
}

static int optical_release(struct inode *inode, struct file *filp) {
  device_context *device;

  mutex_lock(&optical_file_lock);
  device = filp->private_data;
  if (device != NULL) {
    device->file_private_data = NULL;
  }
  filp->private_data = NULL;
  mutex_unlock(&optical_file_lock);

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
  device_context *device;
  unsigned int length;

  device = interrupt_urb->context;

  switch (interrupt_urb->status) {
  case -ECONNRESET:
  case -ENOENT:
  case -ESHUTDOWN:
    return;
  }

  spin_lock(&device->lock);
  if (interrupt_urb->status == 0) {
    if (interrupt_urb->actual_length > 0) {
      length = min_t(unsigned int, interrupt_urb->actual_length,
                     sizeof(device->buffer));
      memcpy(device->buffer, device->ongoing_buffer, length);
      device->buffer_length = length;
    }
  }
  spin_unlock(&device->lock);
  if (device->buffer_length > 0) {
    wake_up_interruptible(&device->read_wait);
  }

  submit_urb(device);
}

static int optical_open_device(struct input_dev *input_dev) {
  device_context *device;

  device = input_get_drvdata(input_dev);
  info("%s", __func__);

  submit_urb(device);
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
  // input_set_drvdata(obj, device);

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
  device_context *device;

  do {
    device = kzalloc(sizeof(device_context), GFP_KERNEL);
    if (device == NULL) {
      err("%s: Out of memory.", __func__);
      break;
    }
    device->file_private_data = NULL;
    device->disconnected = false;

    do {
      device_context_init(device, intf);
      device->input_dev = input_allocate_device();
      if (device->input_dev == NULL) {
        break;
      }
      do {
        spin_lock_init(&device->lock);
        init_waitqueue_head(&device->read_wait);
        device->ongoing_buffer =
            usb_alloc_coherent(device->usb_device, sizeof(device->buffer), GFP_ATOMIC,
                               &device->ongoing_buffer_dma);
        if (device->ongoing_buffer == NULL) {
          break;
        }
        do {
          device->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);
          if (device->interrupt_urb == NULL) {
            break;
          }
          do {
            usb_fill_int_urb(device->interrupt_urb, device->usb_device,
                             device->pipe_input, device->ongoing_buffer,
                             sizeof(device->buffer), on_interrupt, device,
                             device->pipe_interval);
            device->interrupt_urb->transfer_dma = device->ongoing_buffer_dma;
            device->interrupt_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
            device->buffer_length = 0;
            device->interrupt_urb->dev = device->usb_device;
            input_dev_init(device->input_dev, &device->pool, device->usb_device,
                           &intf->dev);
            input_set_drvdata(device->input_dev, device);
            retval = input_register_device(device->input_dev);
            if (retval != 0) {
              break;
            }
            do {
              usb_set_intfdata(intf, device);
              device->intf = intf;
              device->registered = false;
              INIT_DELAYED_WORK(&device->register_work, optical_register_work);
              schedule_delayed_work(&device->register_work,
                                    msecs_to_jiffies(500));
              return 0;
            } while (false);
            // ԭ��û�е���input_unregister_device
            input_unregister_device(device->input_dev);
            device->input_dev = NULL;
          } while (false);
          usb_free_urb(device->interrupt_urb);
        } while (false);
        usb_free_coherent(device->usb_device, sizeof(device->buffer),
                          device->ongoing_buffer, device->ongoing_buffer_dma);
      } while (false);
      if (device->input_dev != NULL) {
        input_free_device(device->input_dev);
      }
    } while (false);
    if (device->file_private_data != NULL) {
      *(device->file_private_data) = NULL;
    }
    device->file_private_data = NULL;
    kfree(device);
  } while (false);
  return -ENOMEM;
}

static void optical_disconnect(struct usb_interface *intf) {
  device_context *device = usb_get_intfdata(intf);

  if (device == NULL) {
    return;
  }

  cancel_delayed_work_sync(&device->register_work);

  mutex_lock(&optical_file_lock);
  if (device->registered) {
    usb_deregister_dev(intf, &optical_class);
    device->registered = false;
  }
  usb_set_intfdata(intf, NULL);
  device->disconnected = true;
  if (device->file_private_data != NULL) {
    (*device->file_private_data) = NULL;
  }
  device->file_private_data = NULL;
  mutex_unlock(&optical_file_lock);

  wake_up_interruptible(&device->read_wait);

  input_unregister_device(device->input_dev);
  device->input_dev = NULL;
  cancel_urb(device);
  usb_free_urb(device->interrupt_urb);
  usb_free_coherent(device->usb_device, sizeof(device->buffer), device->ongoing_buffer,
                    device->ongoing_buffer_dma);
  kfree(device);
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

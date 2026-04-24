#ifndef _OPTICAL_DRV_H_
#define _OPTICAL_DRV_H_

#include <linux/workqueue.h>

#pragma pack(1)

typedef unsigned char OpticalReportTouchPointStateFlag;
#define OpticalReportTouchPointStateFlag_None                                  \
  ((OpticalReportTouchPointStateFlag)0x00)
#define OpticalReportTouchPointStateFlag_IsValid                               \
  ((OpticalReportTouchPointStateFlag)0x01)
#define OpticalReportTouchPointStateFlag_IsTouched                             \
  ((OpticalReportTouchPointStateFlag)0x02)

typedef struct _OpticalReportTouchPoint {
  OpticalReportTouchPointStateFlag state;
  signed short x;
  signed short y;
  signed short width;
  signed short height;
} OpticalReportTouchPoint;

typedef struct _OpticalReportPacketSingleTouch {
  OpticalReportTouchPoint touchPoint;
  unsigned short scanTime;
} OpticalReportPacketSingleTouch;

typedef struct _OpticalReportPacketMultiTouch {
  OpticalReportTouchPoint touchPoint[OPTICAL_TOUCH_POINT_COUNT];
  unsigned short scanTime;
} OpticalReportPacketMultiTouch;

typedef struct _device_context_pool {
  char name[128];
  char phys[64];
} device_context_pool;

typedef struct _device_context {
  struct usb_device *usb_device;
  struct input_dev *input_dev;
  struct device *device;
  struct usb_interface *intf;
  struct delayed_work register_work;
  dev_t dev;
  void **file_private_data;
  int pipe_input;
  unsigned char pipe_interval;

  bool registered;

  struct urb *interrupt_urb;

  spinlock_t lock;

  unsigned char *ongoing_buffer;
  dma_addr_t ongoing_buffer_dma;

  unsigned char buffer_length;
  unsigned char buffer[64];

  device_context_pool pool;
} device_context;

#pragma pack()

// control code
#define OPTICAL_IOCTL_CODE_TYPE_MASK 0x00ff0000u

#define OPTICAL_IOCTL_CODE_TYPE_SET_REPORT 0x00100000u
#define OPTICAL_IOCTL_CODE_TYPE_GET_REPORT 0x00110000u

#define OPTICAL_IOCTL_CODE_TYPE_SYNC_ABSOLUTEMOUSE 0x00200000u
#define OPTICAL_IOCTL_CODE_TYPE_SYNC_SINGLETOUCH 0x00210000u
#define OPTICAL_IOCTL_CODE_TYPE_SYNC_MULTITOUCH 0x00220000u
#define OPTICAL_IOCTL_CODE_TYPE_SYNC_KEYBOARD 0x00230000u

#define OPTICAL_IOCTL_CODE_TYPE_SYNC_DIAGNOSIS 0x00300000u
#define OPTICAL_IOCTL_CODE_TYPE_SYNC_RAWTOUCH 0x00310000u
#define OPTICAL_IOCTL_CODE_TYPE_SYNC_TOUCH 0x00320000u
#define OPTICAL_IOCTL_CODE_TYPE_SYNC_VIRTUALKEY 0x00330000u

#define OPTICAL_IOCTL_CODE_LENGTH_MASK 0x0000ffffu

#define OPTICAL_IOCTL_CODE(type, length)                                       \
  (((type) & OPTICAL_IOCTL_CODE_TYPE_MASK) |                                   \
   ((length) & OPTICAL_IOCTL_CODE_LENGTH_MASK))

#endif // _OPTICAL_DRV_H_

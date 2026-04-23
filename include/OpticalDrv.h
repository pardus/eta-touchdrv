#ifndef _OPTICAL_DRV_H_
#define _OPTICAL_DRV_H_


#pragma pack(1)

typedef unsigned char OpticalReportTouchPointStateFlag;
#define OpticalReportTouchPointStateFlag_None ((OpticalReportTouchPointStateFlag)0x00)
#define OpticalReportTouchPointStateFlag_IsValid ((OpticalReportTouchPointStateFlag)0x01)
#define OpticalReportTouchPointStateFlag_IsTouched ((OpticalReportTouchPointStateFlag)0x02)

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
    struct usb_device* usb_device;
    struct input_dev* input_dev;
    int pipe_input;
    unsigned char pipe_interval;

    struct urb* interrupt_urb;
    struct usb_anchor submitted;
    struct kref kref;
    struct mutex io_lock;
    bool disconnected;

    unsigned char* ongoing_buffer;
    dma_addr_t ongoing_buffer_dma;

    device_context_pool pool;
} device_context;

#pragma pack()

#endif  // _OPTICAL_DRV_H_

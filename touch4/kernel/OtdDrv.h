#ifndef _OTD_DRV_H_
#define _OTD_DRV_H_

#define DEVICE_NODE_NAME    "OtdUsbRaw"
#define OTD_TOUCH_POINT_COUNT 10

#pragma pack(1)

typedef unsigned char OtdReportTouchPointStateFlag;
#define OtdReportTouchPointStateFlag_None       ((OtdReportTouchPointStateFlag)0x00)
#define OtdReportTouchPointStateFlag_IsValid    ((OtdReportTouchPointStateFlag)0x01)
#define OtdReportTouchPointStateFlag_IsTouched  ((OtdReportTouchPointStateFlag)0x02)

typedef struct _OtdReportTouchPoint
{
    OtdReportTouchPointStateFlag state;
    signed short x;
    signed short y;
    signed short width;
    signed short height;
}
OtdReportTouchPoint;

typedef struct _OtdReportPacketMultiTouch
{
    OtdReportTouchPoint touchPoints[OTD_TOUCH_POINT_COUNT];
    unsigned short scanTime;
}
OtdReportPacketMultiTouch;

#pragma pack()

//control code
#define OTD_IOCTL_CODE_TYPE_MASK                        0x00ff0000u

#define OTD_IOCTL_CODE_TYPE_SET_REPORT                  0x00100000u
#define OTD_IOCTL_CODE_TYPE_GET_REPORT                  0x00110000u

#define OTD_IOCTL_CODE_TYPE_SYNC_ABSOLUTEMOUSE          0x00200000u
#define OTD_IOCTL_CODE_TYPE_SYNC_SINGLE_TOUCH           0x00210000u
#define OTD_IOCTL_CODE_TYPE_SYNC_MULTITOUCH             0x00220000u
#define OTD_IOCTL_CODE_TYPE_SYNC_KEYBOARD               0x00230000u

#define OTD_IOCTL_CODE_TYPE_SYNC_DIAGNOSIS              0x00300000u
#define OTD_IOCTL_CODE_TYPE_SYNC_RAWTOUCH               0x00310000u
#define OTD_IOCTL_CODE_TYPE_SYNC_RAWHID                 0x00320000u

#define OTD_IOCTL_CODE_LENGTH_MASK                      0x0000ffffu

#define OTD_IOCTL_CODE(type, length)                    (((type) & OTD_IOCTL_CODE_TYPE_MASK) | ((length) & OTD_IOCTL_CODE_LENGTH_MASK))

#endif // _OTD_DRV_H_

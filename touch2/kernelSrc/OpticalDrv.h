#ifndef _OPTICAL_DRV_H_
#define _OPTICAL_DRV_H_

#define OPTICAL_TOUCH_POINT_COUNT 2

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

#pragma pack()

#endif  // _OPTICAL_DRV_H_

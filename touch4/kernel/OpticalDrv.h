#ifndef _OTD_DRV_H_
#define _OTD_DRV_H_

#define OTD_TOUCH_POINT_COUNT 10

#pragma pack(1)

typedef unsigned char OtdReportTouchPointStateFlag;
#define OtdReportTouchPointStateFlag_None ((OtdReportTouchPointStateFlag)0x00)
#define OtdReportTouchPointStateFlag_IsValid ((OtdReportTouchPointStateFlag)0x01)
#define OtdReportTouchPointStateFlag_IsTouched ((OtdReportTouchPointStateFlag)0x02)

typedef struct _OtdReportTouchPoint {
    OtdReportTouchPointStateFlag state;
    signed short x;
    signed short y;
    signed short width;
    signed short height;
} OtdReportTouchPoint;

typedef struct _OtdReportPacketMultiTouch {
    OtdReportTouchPoint touchPoints[OTD_TOUCH_POINT_COUNT];
    unsigned short scanTime;
} OtdReportPacketMultiTouch;

#pragma pack()

#endif  // _OTD_DRV_H_

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/cdev.h>
#include <asm/uaccess.h> 


/*
 * Version Information
 */
#define DRIVER_VERSION "v1.7.1404.1501"
#define DRIVER_DESC "USB optic touchscreen driver for multi point"
#define DRIVER_LICENSE "GPL"
#define TOUCH_DRIVER_NAME "optictouch"

#define SET_COMMAND 0x01
#define GET_COMMAND 0x02

#define SET_COMMAND_MODE 0x71
#define COMMAND_GET_CAMERAPARA 0x10
#define COMMAND_GET_CALIBPARA 0x14
#define COMMAND_SET_CALIBPARA 0x15
#define SET_WORK_MODE	0x93
#define	GET_CALIB_X	0x2D
#define	SET_CALIB_X	0x2E
#define	GET_CALIB_Y	0x2B
#define	SET_CALIB_Y	0x2C
#define	GET_DEVICE_CONFIG	0x30
#define	SET_DEVICE_CONFIG	0x31
#define GET_FIRMWARE_VER 0x32	
#define	GET_DEVICE_SN	0x34
#define	GET_DEVICE_BARCODE	0x17
#define	GET_DEVICE_BUS	0x1C

#define COMMAND_RETRY_COUNT  8
#define PRIMARYID 	0x00
#define SECONDARYID 	0x01

#define MAJOR_DEVICE_NUMBER 47
#define MINOR_DEVICE_NUMBER 193
#define TOUCH_POINT_COUNT  2
#define USB_TRANSFER_LENGTH			2048
#define TOUCH_DEVICE_NAME "optictouch"

//Control code for firmware update
#define CTLCODE_GET_FIRMWARE_VERSION 0xD0
#define CTLCODE_GET_SERIAL_NUMBER 0xD1
#define CTLCODE_SND_MSG 0xD2
#define CTLCODE_RCV_MSG 0xD3
#define CTLCODE_GET_BIOS_VERSION 0xD4
#define CTLCODE_GET_DRIVER_VERSION 0xD5

#define CTLCODE_GET_COORDINATE 0xc0
#define CTLCODE_SET_CALIB_PARA_X 0xc1
#define CTLCODE_SET_CALIB_PARA_Y 0xc2
#define CTLCODE_GET_CALIB_PARA_X 0xc3
#define CTLCODE_GET_CALIB_PARA_Y 0xc4
#define CTLCODE_START_CALIB 0xc5
#define CTLCODE_GET_CALIB_STATUS 0xc6
#define CTLCODE_DEVICE_RESET 0xc7
#define CTLCODE_GET_SCR_PARA 0xc8
#define CTLCODE_GET_PRODUCT_SN 0xc9
#define CTLCODE_GET_PRODUCT_ID 0xca
#define CTLCODE_SET_SCAN_MODE 0xcb
#define CTLCODE_GET_PRODUCT_BAR 0xcc
#define CTLCODE_SET_DEVICE_CONFIG 0xcd
#define CTLCODE_GET_DEVICE_CONFIG 0xce

#define CTLCODE_SCREEN_PROP 0xF0
#define CTLCODE_PRIMARY_CAMERA 0xF1
#define CTLCODE_SECONDLY_CAMERA 0xF2

#define touch_err(format, arg...)					\
	printk(KERN_ERR KBUILD_MODNAME ": " format "\n", ##arg)

#pragma pack(1)
struct optictouch_point
{
	unsigned char	PointId;
	unsigned char	status;
	short	x;
	short	y;
};

struct points
{
	int	x;
	int	y;
};

struct calib_param{
	long	A00;
	long	A01;
	long	A10;
	unsigned char reserve[46];
};

struct	device_config{
	unsigned char	DeviceID;
	unsigned char	MonitorID;
	unsigned short	MinX;
	unsigned short	MinY;
	unsigned short	MaxX;
	unsigned short	MaxY;
	unsigned char	IsCalibrated;
	unsigned char reserved1[47];
};

struct scr_props
{
	char DefaultOrigin	: 4;
	char PhysicalOrigin	: 4;
    unsigned char s1;		//红外未使用
    int width;				//unit:mm/s
    int height;				//unit:mm/s
 	short lightNo_h;		//横轴灯数
	short lightNo_v;		//纵轴灯数
	unsigned char maxcount;	//支持最大点数
	unsigned char ScanMethod;	//扫描方式 如1对3、1对5等
	unsigned char ScanInterv;	//发射接收最大偏灯数
	union
	{
		unsigned char nCustomized;	//特定版本号
		struct
		{
			unsigned char flags	: 2;	//特定版
			unsigned char Size	: 3;	//屏尺寸类型
			unsigned char Style	: 3;	//直灯/偏灯
		}TypeStatus;
	};

    unsigned short dHLInvalidInterv;	//X方向左边框附近无效区域（mm）(0.00~127.99)
	unsigned short dHRInvalidInterv;	//X方向右边框附近无效区域（mm）(0.00~127.99)
	unsigned short dVTInvalidInterv;	//V方向上边框附近无效区域（mm）(0.00~127.99)
	unsigned short dVBInvalidInterv;	//V方向下边框附近无效区域（mm）(0.00~127.99)
    unsigned short dHLightInterv;	//H灯间距 (0.00~127.99)
	unsigned short dVLightInterv;	//V灯间距 (0.00~127.99)
	unsigned short dMargin;	//边框特殊处理区域大小	(0.00~127.99)
	unsigned short dOffset;	//外扩强度	(0.00~127.99)
	
	int reserve1;
    int reserve2;
    int reserve3;
    int reserve4;

    float reserve5;
    float reserve6;
};

struct product_descriptor	//RKA100W021S
{
	unsigned char 	ProductType;
	unsigned char 	ProductSeries;
	unsigned char 	ProductReplace[4];
	unsigned char 	ScreenType;
	unsigned char 	size[3];
	unsigned char 	standardType;
	unsigned char	Reserved[9];
};

struct AlgContext
{
	int	actualCounter;
	struct optictouch_point inPoint[TOUCH_POINT_COUNT];
};

struct scan_range
{
    unsigned char Status;
    unsigned short   Start;
    unsigned short   End;
};

struct irscan_mode
{
    unsigned char Enable_Switch;   //0:stop 1:direct 2:Oblique 3:direct and Oblique
    unsigned char Scan_Mode;   //0:all 1:trace
    unsigned char Being_Num;   //0:1on1 1:1on3 2:1on5
    unsigned char Offset; 
    unsigned char Track_Width;
    unsigned char DataType;
    struct scan_range HRange[2];
    struct scan_range VRange[2];
    unsigned char ScanOffset;  //0:nothing 1:ScanInterv
    unsigned char reserve;
};

typedef struct _OptCamProps
{
	unsigned char Pos;	//'L' or 'R'
	unsigned char ID;	//'0'='Primary' or '1'='Secondary'
	int 	lowEdge;
	int 	highEdge;
	float 	a0;		//default = 0;
	float 	a1;		//default = 1.15;
	float 	a2;		//default = 0;
	float 	a3;		//default = 0;
	float 	a4;		//default = 0;
	float 	a5;		//default = 0;
	float 	a6;		//default = 0;
	float 	a7;		//default = 0;
	float 	b0;		//default = 0;
	float 	b1;		//default = 1;
	float 	reserve0;
	float 	reserve1;
}OptCamProps, *POptCamProps;

typedef struct _OptScrProps
{
	unsigned char pos;
	unsigned char s1;
	int 	width;		//unit:mm/s
	int 	height;		//unit:mm/s
	int 	fps;
	int 	maxSpeed;	//unit:mm/s
	int 	camOffsetY;	//unit:mm
	int 	camLOffsetX;	//unit:mm
	int 	camROffsetX;	//unit:mm	
	int 	velocityRefCount;    
	float 	veloctiyWeighing;    
	float 	reserve0;
	float 	reserve1;
	float 	reserve2;
	float 	reserve3;
	float 	reserve4;
}OptScrProps, *POptScrProps;

#pragma pack()

struct optictouchusb{
	char name[128];
	char phys[64];
	int pipe;
	struct usb_device *udev;
	struct input_dev *dev;
	char inbuf[USB_TRANSFER_LENGTH];
};

struct device_context{
	bool startCalib;
	bool updateParam;
	u16  productid;
	struct optictouch_point points;
	struct AlgContext algCtx;
	struct calib_param calibX;
	struct calib_param calibY;
	struct device_config devConfig;
	struct scr_props     ScreenProps;
	struct product_descriptor	ProductSN;
	char firmwareVer[10];
	char	BarCodeInfo[40];
	OptScrProps optScreenProps;
	OptCamProps PrimaryCamProps;
	OptCamProps SecondaryCamProps;
	struct optictouchusb *optictouch;
};

struct cdev optictouch_cdev;
struct device_context *optictouchdev_context = NULL;
static struct class *irser_class;
static struct usb_driver optictouch_driver;
static unsigned char SetScanModeBuffer[2][64];

static void optictouch_translatePoint(struct optictouch_point pt, int *x, int *y)
{
	*x = ((optictouchdev_context->calibX.A01 * pt.y) / 10000) + ((optictouchdev_context->calibX.A10 * pt.x) / 10000) + optictouchdev_context->calibX.A00;
	*y = ((optictouchdev_context->calibY.A01 * pt.y) / 10000) + ((optictouchdev_context->calibY.A10 * pt.x) / 10000) + optictouchdev_context->calibY.A00;
}

static int optictouch_build_packet(
		unsigned char 	command_type, 
		unsigned char 	command, 
		int 		device_id, 
		unsigned char * data, 
		int 		length, 
		unsigned char * out_data
		)
{
	unsigned char 	tmp = 0x55;
	unsigned char 	package[64] = {0};
	int		out_length = 0;
	int i;

	package[0] = 0xaa;
	package[1] = command_type;
	package[2] = command;
	package[3] = (unsigned char)length + 1;
	package[4] = (unsigned char)device_id;

	memcpy(&package[5],data,length);

	for(i = 0; i < 5 + length; i++)
	{
		tmp = package[i] + tmp;
	}

	package[5 + length] = (unsigned char)tmp;

	out_length = 6 + length;

	memcpy(out_data, package, out_length);

	return out_length;
}

unsigned char optictouch_send_command(struct usb_device * udev,
			unsigned char 	command_type,
			unsigned char * in_data,
			unsigned char * out_data,
			int 		length
		)
{
	unsigned char	buf[64];
	int		count = 0;
	int		ret = 0;
	unsigned char	result = 0x00;

	do{
		memset(buf,0,sizeof(buf));
		ret = usb_control_msg(udev, 
					usb_sndctrlpipe(udev, 0), 
					0, 
					USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					0, 
					0, 
					(char *)in_data,
					length, 
					1000);
		msleep(50);

		ret = usb_control_msg(udev, 
					usb_rcvctrlpipe(udev, 0), 
					0, 
					0xc0, 
					0, 
					0, 
					(char *)buf, 
					64, 
					1000);
		msleep(200);

		count++;
		result = (buf[1] >> 4) & 0x03;
	}while((0x01 != result) && (count < COMMAND_RETRY_COUNT));

	if(count >= COMMAND_RETRY_COUNT)
	{
		return false;
	}

	memcpy(out_data,buf,sizeof(buf));

	return true;
	
}

static void optictouch_set_scanmode(struct usb_device * udev, struct irscan_mode *scanmode)
{
	int	ret = 0;
	unsigned char inBuffer[64] = { 0 };
	unsigned char inBufferLen = 0;

	inBufferLen = optictouch_build_packet(SET_COMMAND, 0x39, 0, (unsigned char *)scanmode, sizeof(struct irscan_mode), inBuffer);
	//if(memcmp(SetScanModeBuffer[scanmode->Scan_Mode], inBuffer, inBufferLen) == 0)
	//{
	//	return;
	//}

	memcpy(SetScanModeBuffer[scanmode->Scan_Mode], inBuffer, inBufferLen);
	ret = usb_control_msg(udev, 
					usb_sndctrlpipe(udev, 0), 
					0, 
					USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					0, 
					0, 
					(char *)inBuffer,
					inBufferLen, 
					1000);
}

static void optictouch_get_device_param(struct usb_device * udev)
{
	unsigned char	inData[58];
	unsigned char	outData[64];
	unsigned char	getData[64];
	int status = -1;
	int length = 0;

	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	//set touchscreen to command mode
	inData[0] = 0x01;

	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Set command mode failed.",__func__);
	}
	
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	inData[0] = 0xF4;

	length = optictouch_build_packet(SET_COMMAND,SET_WORK_MODE,0,inData,1,outData);
	status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Set device to work status failed.",__func__);
	}

	//get device configuration
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,0,inData,0,outData);
	status = optictouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Get device configuration failed.",__func__);
	}
	memcpy(&optictouchdev_context->ScreenProps,&getData[5],sizeof(struct scr_props));
	printk("maxcount=%d!\n", optictouchdev_context->ScreenProps.maxcount);
	if(0 == optictouchdev_context->ScreenProps.maxcount)
		optictouchdev_context->ScreenProps.maxcount = TOUCH_POINT_COUNT;
	
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,1,inData,0,outData);
	status = optictouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Get device configuration failed.",__func__);
	}
	memcpy(&optictouchdev_context->devConfig,&getData[5],sizeof(struct device_config));

	//get calib x parameter
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(GET_COMMAND,GET_CALIB_X,0,inData,0,outData);
	status = optictouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Get calib X param failed.",__func__);
	}
	memcpy(&optictouchdev_context->calibX,&getData[5],sizeof(struct calib_param));

	//get calib y parameter
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(GET_COMMAND,GET_CALIB_Y,0,inData,0,outData);
	status = optictouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Get calib Y param failed.",__func__);
	}
	memcpy(&optictouchdev_context->calibY,&getData[5],sizeof(struct calib_param));

	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(GET_COMMAND,GET_DEVICE_SN,0,inData,0,outData);
	status = optictouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Get device ProductSN failed.",__func__);
	}
	memcpy(&optictouchdev_context->ProductSN,&getData[5],sizeof(struct product_descriptor));

	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(GET_COMMAND,GET_FIRMWARE_VER,0,inData,0,outData);
	status = optictouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(status)
	{
		memset(optictouchdev_context->firmwareVer,0,sizeof(optictouchdev_context->firmwareVer));		
		if(getData[3] == 3)
		{
			sprintf(optictouchdev_context->firmwareVer, "%d.%d",getData[6],getData[5]);
		}
		else if(getData[3] == 5)
		{
			sprintf(optictouchdev_context->firmwareVer, "%d.%d.%04d",getData[6],getData[5],(getData[8]<<8|getData[7]));
		}
	}
	else
	{
		touch_err("ODT:    %s  get firmware version failed.",__func__);
	}
	printk("ODT MCU version is: %s.\n", optictouchdev_context->firmwareVer);
	
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(GET_COMMAND,GET_DEVICE_BARCODE,0,inData,0,outData);
	status = optictouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Get device BARCODE failed.",__func__);
	}
	memcpy(optictouchdev_context->BarCodeInfo,&getData[5],40);

	//set touchscreen to work mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Set to work mode failed.",__func__);
	}
}

static void optictouch_get_opt_param(struct usb_device * udev)
{
	unsigned char	in_data[58];
	unsigned char	out_data[64];
	unsigned char	get_data[64];
	unsigned char	status = false;
	int		length = 0;

	//Set primary camera to graphics mode
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	in_data[0] = 0x01;

	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,PRIMARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		touch_err("ODT:    %s Set primary camera to graphics mode fail.",__func__);
	}	
	
	//Set primary camera to graphics mode
	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,SECONDARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		touch_err("ODT:    %s Set secondary camera to graphics mode fail.",__func__);
	}

	msleep(100);	
	//Get primary camera parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = optictouch_build_packet(GET_COMMAND,COMMAND_GET_CAMERAPARA,PRIMARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	if(status)
	{
		memcpy(&optictouchdev_context->PrimaryCamProps,&get_data[5],58);
	}
	else
	{
		touch_err("ODT:    %s Get primary camera parameter fail.",__func__);
	}
	
	//Get secondary camera parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = optictouch_build_packet(GET_COMMAND,COMMAND_GET_CAMERAPARA,SECONDARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	if(status)
	{	
		memcpy(&optictouchdev_context->SecondaryCamProps,&get_data[5],58);	
	}
	else
	{
		touch_err("ODT:    %s Get secondary camera parameter fail.",__func__);
	}

	//Get Product parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = optictouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,PRIMARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,GET_COMMAND,out_data,get_data,length);	
	if(status)
	{
		memcpy(&optictouchdev_context->optScreenProps,&get_data[5],58);	
	}
	else
	{
		touch_err("ODT:    %s Get Product parameter fail.",__func__);
	}

	//Get Device Config
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = optictouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,SECONDARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	if(status)
	{
		memcpy(&optictouchdev_context->devConfig,&get_data[5],58);
	}
	else
	{
		touch_err("ODT:    %s Get Device Config fail.",__func__);
	}

	//Get X Calibration Parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = optictouch_build_packet(GET_COMMAND,COMMAND_GET_CALIBPARA,PRIMARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,GET_COMMAND,out_data,get_data,length);	
	if(status)
	{
		memcpy(&optictouchdev_context->calibX,&get_data[5],58);	
	}
	else
	{
		touch_err("ODT:    %s Get X Calibration Parameter fail.",__func__);
	}

	//Get Y Calibration Parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = optictouch_build_packet(GET_COMMAND,COMMAND_GET_CALIBPARA,SECONDARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,GET_COMMAND,out_data,get_data,length);	
	if(status)
	{
		memcpy(&optictouchdev_context->calibY,&get_data[5],58);
	}
	else
	{
		touch_err("ODT:    %s Get Y Calibration Parameter fail.",__func__);
	}

	//memset(in_data,0,sizeof(in_data));
	//memset(out_data,0,sizeof(out_data));
	//memset(get_data,0,sizeof(get_data));

	//length = optictouch_build_packet(GET_COMMAND,GET_DEVICE_SN,0,in_data,0,out_data);
	//status = optictouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	//if(!status)
	//{
	//	touch_err("ODT:    %s  Get device ProductSN failed.",__func__);
	//}
	//memcpy(&optictouchdev_context->ProductSN,&get_data[5],sizeof(struct product_descriptor));
	
	//Set the primary camera of failure to coordinate mode
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));

	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,PRIMARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		touch_err("ODT:    %s Set the primary camera of failure to coordinate mode fail.",__func__);
	}

	//Set the secondary camera of failure to coordinate mode
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));

	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,SECONDARYID,in_data,1,out_data);
	status = optictouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		touch_err("ODT:    %s Set the secondary camera of failure to coordinate mode fail.",__func__);
	}
	optictouchdev_context->ScreenProps.maxcount = 2;
}

static void optictouch_set_calib(struct usb_device * udev, bool resetFlg)
{
	unsigned char	inData[58];
	unsigned char	outData[64];
	unsigned char	getData[64];
	int status = -1;
	int length = 0;

	//set touchscreen to command mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	inData[0] = 0x01;

	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Set to command mode failed.",__func__);
	}

	if(optictouchdev_context->productid == 0x0c20)
	{
		length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,1,inData,1,outData);
		status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("ODT:    %s  Set to command mode failed.",__func__);
		}
	}
	
	//Set device configuration
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));

	if(resetFlg)
	{
		optictouchdev_context->devConfig.IsCalibrated = 0;
		memcpy(&inData,&optictouchdev_context->devConfig,58);
	
		length = optictouch_build_packet(SET_COMMAND,SET_DEVICE_CONFIG,1,inData,58,outData);
		status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("ODT:    %s  Set device configuration failed.",__func__);
		}
	}
	else
	{
		//optictouchdev_context->devConfig.IsCalibrated = 1;
		memcpy(&inData,&optictouchdev_context->devConfig,58);
	
		length = optictouch_build_packet(SET_COMMAND,SET_DEVICE_CONFIG,1,inData,58,outData);
		status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("ODT:    %s  Set device configuration failed.",__func__);
		}

		//Set calib X param
		memset(inData,0,sizeof(inData));
		memset(outData,0,sizeof(outData));

		memcpy(&inData,&optictouchdev_context->calibX,58);

		if(optictouchdev_context->productid == 0x0c20)
		{
			length = optictouch_build_packet(SET_COMMAND,COMMAND_SET_CALIBPARA,PRIMARYID,inData,58,outData);
			status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				touch_err("ODT:    %s Set X camera para fail.",__func__);
			}
		}
		else
		{
			length = optictouch_build_packet(SET_COMMAND,SET_CALIB_X,0,inData,58,outData);
			status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				touch_err("ODT:    %s  Set calib X param failed.",__func__);
			}
		}
		//Set calib Y param
		memset(inData,0,sizeof(inData));
		memset(outData,0,sizeof(outData));

		memcpy(&inData,&optictouchdev_context->calibY,58);

		if(optictouchdev_context->productid == 0x0c20)
		{
			length = optictouch_build_packet(SET_COMMAND,COMMAND_SET_CALIBPARA,SECONDARYID,inData,58,outData);
			status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				touch_err("ODT:    %s Set X camera para fail.",__func__);
			}
		}
		else
		{
			length = optictouch_build_packet(SET_COMMAND,SET_CALIB_Y,0,inData,58,outData);
			status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				touch_err("ODT:    %s  Set calib Y param failed.",__func__);
			}
		}
	}

	//set touchscreen to work mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));

	length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);	
	status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("ODT:    %s  Set to work mode failed.",__func__);
	}

	if(optictouchdev_context->productid == 0x0c20)
	{
		length = optictouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,1,inData,1,outData);
		status = optictouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("ODT:    %s  Set to command mode failed.",__func__);
		}
	}
}

static int optictouch_open(struct inode * inode, struct file * filp)
{
	struct optictouchusb *optictouch;
	
	optictouch = optictouchdev_context->optictouch;
	if(!optictouch)
	{
		touch_err("ODT:    %s touch is NULL!",__func__);
		return -1;
	}

	filp->private_data = optictouch;
	//printk("ODT:    %s success!",__func__);
	return 0;
}

static int optictouch_release(struct inode * inode, struct file * filp)
{
	return 0;
}

static int optictouch_read(struct file * filp, char * buffer, size_t count, loff_t * ppos)
{
	int ret = -1;
	int bytes_read;
	struct optictouchusb * optictouch;
	
	optictouch = filp->private_data;
	ret = usb_interrupt_msg(optictouch->udev,
				optictouch->pipe,
				optictouch->inbuf,
				USB_TRANSFER_LENGTH,
				&bytes_read, 10000);
	
	if(!ret)
	{
		if(optictouchdev_context->startCalib)
		{
			optictouch->inbuf[0] = 0xa0;
		}
		else if(optictouchdev_context->updateParam)
		{
			optictouchdev_context->updateParam = false;
			optictouch->inbuf[0] = 0xa1;
		}

		if(copy_to_user(buffer, &optictouch->inbuf, bytes_read))
		{
			touch_err("ODT:  %s copy_to_user failed!",__func__);
			ret = -EFAULT;
		}
		else
		{
			ret = bytes_read;
		}
	}
	else if(ret == -ETIMEDOUT || ret == -ETIME)
	{
		return ret;
	}
	else 
	{
		touch_err("ODT:  %s usb_interrupt_msg failed %d!",__func__, ret);
	}
	
	return ret;	
}

static void optictouch_process_data(struct optictouchusb *optictouch)
{	
	int i;
	unsigned int x = 0, y = 0;
	int count = 0;
	struct input_dev *dev = optictouch->dev;
	
	for(i=0;i<optictouchdev_context->ScreenProps.maxcount;i++)
	{
		if(optictouchdev_context->startCalib)
		{
			optictouchdev_context->points = optictouchdev_context->algCtx.inPoint[0];
			optictouchdev_context->points.status = optictouchdev_context->points.status | 0x06;
		}

		if(optictouchdev_context->algCtx.inPoint[i].status == 0x01)
		{
			x = optictouchdev_context->algCtx.inPoint[i].x;
			y = optictouchdev_context->algCtx.inPoint[i].y;

			/*	
			if(optictouchdev_context->devConfig.IsCalibrated && !optictouchdev_context->startCalib)
			{
				optictouch_translatePoint(optictouchdev_context->algCtx.inPoint[i],&x,&y);
			}
			*/

			input_report_abs(dev,ABS_MT_TRACKING_ID,optictouchdev_context->algCtx.inPoint[i].PointId);
			input_report_abs(dev,ABS_MT_POSITION_X, x);
			input_report_abs(dev,ABS_MT_POSITION_Y, y);

			input_report_abs(dev,ABS_MT_TOUCH_MAJOR, max(x, y));
			input_report_abs(dev,ABS_MT_TOUCH_MINOR, min(x, y));
		}
		else
		{
			input_report_abs(dev,ABS_MT_TOUCH_MAJOR, 0);
		}
		input_mt_sync(dev);
	}

	/* SYN_MT_REPORT only if no contact */
	if (!count)
	//	input_mt_sync(dev);
	
	input_sync(dev);
}

static int optictouch_write(struct file * filp, const char * user_buffer, size_t count, loff_t * ppos)
{
	int ret = -1;
	struct optictouchusb *optictouch;
	
	optictouch = filp->private_data;
	if(copy_from_user(optictouchdev_context->algCtx.inPoint, user_buffer, sizeof(struct optictouch_point)*optictouchdev_context->ScreenProps.maxcount))
	{
		touch_err("ODT: copy_from_user failed!\n");
		ret = -EFAULT;
	}
	else
	{
		ret = count;
		optictouch_process_data(optictouch);
	}
	//printk("ODT: optictouch_write,ret=%d!\n", ret);
	return ret;	
}

static long optictouch_ioctl(struct file * filp, unsigned int ctl_code, unsigned long ctl_param)
{
	unsigned char value = 0;
	int ret = -1;
	struct optictouchusb *optictouch;

	optictouch = filp->private_data;

	switch(ctl_code)
	{
		case CTLCODE_START_CALIB:				
			ret = copy_from_user(&value, (unsigned char *)ctl_param, sizeof(unsigned char));
			if(ret == 0)
			{
				if(value == 0x01)
				{
					optictouchdev_context->startCalib = true;
				}
				else
				{
					optictouchdev_context->startCalib = false;
				}
			}	
			
			break;

		case CTLCODE_GET_COORDINATE:
			ret = copy_to_user((struct optictouch_point *)ctl_param, &optictouchdev_context->points, sizeof(struct optictouch_point));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_COORDINATE>copy_to_user failed!",__func__);
			}
			memset(&optictouchdev_context->points, 0, sizeof(struct optictouch_point));
			break;

		case CTLCODE_GET_CALIB_STATUS:
			{
				int status = optictouchdev_context->devConfig.IsCalibrated;
				ret = copy_to_user((int *)ctl_param, &status, sizeof(int));
				if(ret != 0)
				{
					touch_err("ODT:    %s <CTLCODE_GET_CALIB_STATUS>copy_to_user failed!",__func__);
				}
				break;
			}
	
		case CTLCODE_SET_CALIB_PARA_X:
			//optictouchdev_context->devConfig.IsCalibrated = 1;
			ret = copy_from_user(&optictouchdev_context->calibX, (struct calib_param *)ctl_param, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_SET_CALIB_PARA_X>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_SET_CALIB_PARA_Y:
			//optictouchdev_context->devConfig.IsCalibrated = 1;
			ret = copy_from_user(&optictouchdev_context->calibY, (struct calib_param *)ctl_param, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_SET_CALIB_PARA_Y>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_CALIB_PARA_X:
			ret = copy_to_user((struct calib_param *)ctl_param, &optictouchdev_context->calibX, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_CALIB_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_CALIB_PARA_Y:
			ret = copy_to_user((struct calib_param *)ctl_param, &optictouchdev_context->calibY, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_CALIB_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_SCR_PARA:
			ret = copy_to_user((struct calib_param *)ctl_param, &optictouchdev_context->ScreenProps, sizeof(struct scr_props));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_SCR_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_PRODUCT_SN:
			ret = copy_to_user((struct calib_param *)ctl_param, &optictouchdev_context->ProductSN, sizeof(struct product_descriptor));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_PRODUCT_SN>copy_to_user failed!",__func__);
			}
			break;
			
		case CTLCODE_GET_FIRMWARE_VERSION:
			ret = copy_to_user((char*)ctl_param, optictouchdev_context->firmwareVer, sizeof(optictouchdev_context->firmwareVer));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_FIRMWARE_VERSION>copy_to_user failed!",__func__);
			}
			break;
			
		case CTLCODE_GET_PRODUCT_BAR:
			ret = copy_to_user((unsigned char *)ctl_param, optictouchdev_context->BarCodeInfo, 40);
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_PRODUCT_BAR>copy_to_user failed!",__func__);
			}
			break;
			
		case CTLCODE_SET_SCAN_MODE:
			{
				struct irscan_mode scanmode = {0};
				ret = copy_from_user(&scanmode, (unsigned char *)ctl_param, sizeof(struct irscan_mode));
				if(ret != 0)
				{
					touch_err("ODT:    %s <CTLCODE_GET_CALIB_STATUS>copy_to_user failed!",__func__);
				}
				else
				{
					optictouch_set_scanmode(optictouch->udev, &scanmode);
				}
				break;
			}
			
		case CTLCODE_GET_PRODUCT_ID:
			{
				int status = optictouchdev_context->productid;
				ret = copy_to_user((int *)ctl_param, &status, sizeof(int));
				if(ret != 0)
				{
					touch_err("ODT:    %s <CTLCODE_GET_PRODUCT_ID>copy_to_user failed!",__func__);
				}
				break;
			}
		
		case CTLCODE_DEVICE_RESET:
			ret = copy_from_user(&value, (unsigned char *)ctl_param, sizeof(unsigned char));
			if(ret == 0)
			{
				optictouchdev_context->updateParam = true;

				if(value == 0x01)
				{
					optictouch_set_calib(optictouch->udev,true);
				}
				else
				{
					optictouch_set_calib(optictouch->udev,false);
				}
			}
			break;

		case CTLCODE_SCREEN_PROP:
			ret = copy_to_user((OptScrProps *)ctl_param, &optictouchdev_context->optScreenProps, sizeof(OptScrProps));	
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_SCREEN_PROP>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_PRIMARY_CAMERA:

			ret = copy_to_user((OptCamProps *)ctl_param, &optictouchdev_context->PrimaryCamProps, sizeof(OptCamProps));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_PRIMARY_CAMERA>copy_to_user failed!",__func__);
			}
			break;
			
		case CTLCODE_SECONDLY_CAMERA:
			ret = copy_to_user((OptCamProps *)ctl_param, &optictouchdev_context->SecondaryCamProps, sizeof(OptCamProps));	
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_SECONDLY_CAMERA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_SET_DEVICE_CONFIG:
			ret = copy_from_user(&optictouchdev_context->devConfig, (struct calib_param *)ctl_param, sizeof(struct device_config));
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_SET_DEVICE_CONFIG>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_DEVICE_CONFIG:
			ret = copy_to_user((OptCamProps *)ctl_param, &optictouchdev_context->devConfig, sizeof(struct device_config));	
			if(ret != 0)
			{
				touch_err("ODT:    %s <CTLCODE_GET_DEVICE_CONFIG>copy_to_user failed!",__func__);
			}
			break;

		default:
			break;
	}

	return 0;
}


static struct file_operations optictouch_fops = {
	.owner = THIS_MODULE,
	.open = optictouch_open,
	.read = optictouch_read,
	.write = optictouch_write,
	.unlocked_ioctl = optictouch_ioctl,
	.release = optictouch_release,
};

static int optictouch_open_device(struct input_dev * dev)
{
	return 0;
}

static void optictouch_close_device(struct input_dev * dev)
{
//	dbg("ODT:  optictouch_close_device+++\n");
}

static bool optictouch_mkdev(void)
{
	int retval;

	//create device node
	dev_t devno = MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER);

	retval = register_chrdev_region(devno,1,TOUCH_DEVICE_NAME);
	if(retval < 0)
	{
		touch_err("ODT:    %s register chrdev error.",__func__);
		return false;
	}

	cdev_init(&optictouch_cdev,&optictouch_fops);
	optictouch_cdev.owner = THIS_MODULE;
	optictouch_cdev.ops = &optictouch_fops;
	retval = cdev_add(&optictouch_cdev,devno,1);

	if(retval)
	{
		touch_err("ODT:    %s  Adding char_reg_setup_cdev error=%d",__func__,retval);
		return false;
	}

	irser_class = class_create(THIS_MODULE, TOUCH_DEVICE_NAME);
	if(IS_ERR(irser_class))
	{
		touch_err("ODT:    %s class create failed.",__func__);
		return false;
	}
	
	device_create(irser_class,NULL,MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER),NULL,TOUCH_DEVICE_NAME);
	
	printk("ODT:    %s success!\n",__func__);
	
	return true;
}

static int optictouch_probe(struct usb_interface * intf, const struct usb_device_id *id)
{
	int i;
	int ret = -ENOMEM;
	struct usb_host_interface * interface = intf->cur_altsetting;
	struct usb_endpoint_descriptor * endpoint = NULL;
	struct usb_device * udev = interface_to_usbdev(intf);
	struct input_dev * dev = NULL;
	struct optictouchusb * optictouch;

	if (!(optictouchdev_context = kmalloc(sizeof(struct device_context), GFP_KERNEL)))  
		return -ENOMEM;
	memset(optictouchdev_context,0,sizeof(struct device_context));
	
	optictouch = kzalloc(sizeof(struct optictouchusb), GFP_KERNEL);
	if(!optictouch)
	{
		touch_err("ODT:    %s Out of memory.",__func__);
		goto EXIT;
	}
	memset(optictouch, 0, sizeof(struct optictouchusb));
	optictouchdev_context->optictouch = optictouch;
	
	for(i = 0; i < interface->desc.bNumEndpoints; i++)
	{
		endpoint = &interface->endpoint[i].desc;
		if(endpoint->bEndpointAddress & USB_DIR_IN)
		{
			if(endpoint->bmAttributes == 3)
				optictouch->pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
			else
				optictouch->pipe = usb_rcvbulkpipe(udev, endpoint->bEndpointAddress);
		}
	}

	dev = input_allocate_device();
	if(!dev)
		goto EXIT;

	if(udev->manufacturer)
		strlcpy(optictouch->name, udev->manufacturer, sizeof(optictouch->name));

	if(udev->product) 
	{
		if (udev->manufacturer)
			strlcat(optictouch->name, " ", sizeof(optictouch->name));
		strlcat(optictouch->name, udev->product, sizeof(optictouch->name));
	}

	if (!strlen(optictouch->name))
		snprintf(optictouch->name, sizeof(optictouch->name),
				"ODT Multi-touch Touchscreen %04x:%04x",
				le16_to_cpu(udev->descriptor.idVendor),
				le16_to_cpu(udev->descriptor.idProduct));

	usb_make_path(udev, optictouch->phys, sizeof(optictouch->phys));
	strlcat(optictouch->phys, "/input0", sizeof(optictouch->phys));

	dev->name = optictouch->name;
	dev->phys = optictouch->phys;

	usb_to_input_id(udev, &dev->id);
	dev->dev.parent = &intf->dev;

	input_set_drvdata(dev, optictouch);

	dev->open = optictouch_open_device;
	dev->close = optictouch_close_device;

	dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	set_bit(BTN_TOUCH, dev->keybit);
	set_bit(EV_SYN,dev->evbit);
	set_bit(EV_KEY,dev->evbit);
	set_bit(EV_ABS,dev->evbit);
	dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y);
	set_bit(ABS_PRESSURE, dev->absbit);
			
	input_set_abs_params(dev, ABS_X, 0, 32767, 0, 0); 
	input_set_abs_params(dev, ABS_Y, 0, 32767, 0, 0);
	input_set_abs_params(dev, ABS_PRESSURE, 0, 1, 0, 0);
	input_set_abs_params(dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_X, 0, 32767, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, 32767, 0, 0);
	input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);	
	input_set_abs_params(dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);

	optictouch->udev = udev;
	optictouch->dev = dev;
	
	ret = input_register_device(optictouch->dev);
	if(ret)
	{
		touch_err("ODT:    %s input_register_device failed,ret:%d",__func__,ret);
		goto EXIT;
	}

	usb_set_intfdata(intf, optictouch);
	msleep(300);

	optictouchdev_context->productid = udev->descriptor.idProduct;
	if(optictouchdev_context->productid == 0x0c20)
		optictouch_get_opt_param(udev);
	else
		optictouch_get_device_param(udev);
	optictouch_mkdev();
	printk("ODT id=0x%x: %s success!\n", optictouchdev_context->productid, __func__);
	return 0;

EXIT:
	if(optictouchdev_context)
		kfree(optictouchdev_context);
	
	if(dev)
		input_free_device(dev);

	if(optictouch)
		kfree(optictouch);

	return -ENOMEM;
}

static void optictouch_disconnect(struct usb_interface * intf)
{
	struct optictouchusb * optictouch = usb_get_intfdata(intf);

	dev_t devno = MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER);

	cdev_del(&optictouch_cdev);
	unregister_chrdev_region(devno,1);

	device_destroy(irser_class,devno);
	class_destroy(irser_class);

	usb_set_intfdata(intf, NULL);

	if(optictouchdev_context)
	{
		kfree(optictouchdev_context);
	}
	
	if(optictouch)
	{
		input_unregister_device(optictouch->dev);
		kfree(optictouch);
		optictouch = NULL;
	}
}

static struct usb_device_id optictouch_table[] = {
	{USB_DEVICE(0x6615, 0x0084)},
	{USB_DEVICE(0x6615, 0x0085)},
	{USB_DEVICE(0x6615, 0x0086)},
	{USB_DEVICE(0x6615, 0x0087)},
	{USB_DEVICE(0x6615, 0x0088)},
	{USB_DEVICE(0x6615, 0x0c20)},
	{ }    /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, optictouch_table);

static struct usb_driver optictouch_driver = {
	.name = TOUCH_DRIVER_NAME,
	.probe = optictouch_probe,
	.disconnect = optictouch_disconnect,
	.id_table = optictouch_table,
};

static int __init optictouch_init(void)
{
	int retval = usb_register(&optictouch_driver);
	if (retval == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":" DRIVER_DESC);
	return retval;
}

static void __exit optictouch_exit(void)
{
	usb_deregister(&optictouch_driver);
}


module_init(optictouch_init);
module_exit(optictouch_exit);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(DRIVER_VERSION);



/* BMA255 motion sensor driver
 *
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2011 Bosch Sensortec GmbH
 * All Rights Reserved
 *
 * VERSION: V1.3
 * HISTORY: V1.0 --- Driver creation
 *          V1.1 --- Add share I2C address function
 *          V1.2 --- Fix the bug that sometimes sensor is stuck after system resume.
 *          V1.3 --- Add FIFO interfaces.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
//#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>
#include <linux/module.h>

#include "upmu_sw.h"
#include "upmu_common.h"
#include "batch.h"


#if defined(MT6573) || defined(MT6575) || defined(MT6577) || defined(MT6589) || defined(MT6592)
#define POWER_NONE_MACRO MT65XX_POWER_NONE
#elif defined(MT6516)
#define POWER_NONE_MACRO MT6516_POWER_NONE
#else
#define POWER_NONE_MACRO MT65XX_POWER_NONE
#endif

#ifdef CONFIG_OF
#define USE_NEW_SENSOR_ARCH
#include <accel.h>
#endif

#include <cust_acc.h>
#include "bma056.h"

/*----------------------------------------------------------------------------*/
#define I2C_DRIVERID_BMA255 255
/*----------------------------------------------------------------------------*/
#define DEBUG 1
/*----------------------------------------------------------------------------*/
//#define CONFIG_BMA255_LOWPASS   /*apply low pass filter on output*/       
#define SW_CALIBRATION
#define CONFIG_I2C_BASIC_FUNCTION
#define MAX_FIFO_F_LEVEL 32
#define MAX_FIFO_F_BYTES 6

/*----------------------------------------------------------------------------*/
#define BMA255_AXIS_X          0
#define BMA255_AXIS_Y          1
#define BMA255_AXIS_Z          2
#define BMA255_AXES_NUM        3
#define BMA255_DATA_LEN        6
#define BMA255_DEV_NAME        "BMA255"

#define BMA255_MODE_NORMAL      0
#define BMA255_MODE_LOWPOWER    1
#define BMA255_MODE_SUSPEND     2

#define BMA255_ACC_X_LSB__POS           4
#define BMA255_ACC_X_LSB__LEN           4
#define BMA255_ACC_X_LSB__MSK           0xF0
//#define BMA255_ACC_X_LSB__REG           BMA255_X_AXIS_LSB_REG

#define BMA255_ACC_X_MSB__POS           0
#define BMA255_ACC_X_MSB__LEN           8
#define BMA255_ACC_X_MSB__MSK           0xFF
//#define BMA255_ACC_X_MSB__REG           BMA255_X_AXIS_MSB_REG

#define BMA255_ACC_Y_LSB__POS           4
#define BMA255_ACC_Y_LSB__LEN           4
#define BMA255_ACC_Y_LSB__MSK           0xF0
//#define BMA255_ACC_Y_LSB__REG           BMA255_Y_AXIS_LSB_REG

#define BMA255_ACC_Y_MSB__POS           0
#define BMA255_ACC_Y_MSB__LEN           8
#define BMA255_ACC_Y_MSB__MSK           0xFF
//#define BMA255_ACC_Y_MSB__REG           BMA255_Y_AXIS_MSB_REG

#define BMA255_ACC_Z_LSB__POS           4
#define BMA255_ACC_Z_LSB__LEN           4
#define BMA255_ACC_Z_LSB__MSK           0xF0
//#define BMA255_ACC_Z_LSB__REG           BMA255_Z_AXIS_LSB_REG

#define BMA255_ACC_Z_MSB__POS           0
#define BMA255_ACC_Z_MSB__LEN           8
#define BMA255_ACC_Z_MSB__MSK           0xFF
//#define BMA255_ACC_Z_MSB__REG           BMA255_Z_AXIS_MSB_REG

#define BMA255_EN_LOW_POWER__POS          6
#define BMA255_EN_LOW_POWER__LEN          1
#define BMA255_EN_LOW_POWER__MSK          0x40
#define BMA255_EN_LOW_POWER__REG          BMA255_REG_POWER_CTL

#define BMA255_EN_SUSPEND__POS            7
#define BMA255_EN_SUSPEND__LEN            1
#define BMA255_EN_SUSPEND__MSK            0x80
#define BMA255_EN_SUSPEND__REG            BMA255_REG_POWER_CTL

#define BMA255_RANGE_SEL__POS             0
#define BMA255_RANGE_SEL__LEN             4
#define BMA255_RANGE_SEL__MSK             0x0F
#define BMA255_RANGE_SEL__REG             BMA255_REG_DATA_FORMAT

#define BMA255_BANDWIDTH__POS             0
#define BMA255_BANDWIDTH__LEN             5
#define BMA255_BANDWIDTH__MSK             0x1F
#define BMA255_BANDWIDTH__REG             BMA255_REG_BW_RATE

/* fifo mode*/
#define BMA255_FIFO_MODE__POS                 6
#define BMA255_FIFO_MODE__LEN                 2
#define BMA255_FIFO_MODE__MSK                 0xC0
#define BMA255_FIFO_MODE__REG                 BMA255_FIFO_MODE_REG

#define BMA255_FIFO_FRAME_COUNTER_S__POS             0
#define BMA255_FIFO_FRAME_COUNTER_S__LEN             7
#define BMA255_FIFO_FRAME_COUNTER_S__MSK             0x7F
#define BMA255_FIFO_FRAME_COUNTER_S__REG             BMA255_STATUS_FIFO_REG

#define BMA255_GET_BITSLICE(regvar, bitname)\
	((regvar & bitname##__MSK) >> bitname##__POS)

#define BMA255_SET_BITSLICE(regvar, bitname, val)\
	((regvar & ~bitname##__MSK) | ((val<<bitname##__POS)&bitname##__MSK))

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static const struct i2c_device_id bma255_i2c_id[] = {{BMA255_DEV_NAME,0},{}};
//static struct i2c_board_info __initdata bma255_i2c_info ={ I2C_BOARD_INFO(BMA255_DEV_NAME, BMA255_I2C_ADDR)};

#define BMA056_NAME "mediatek,bma056"
/*----------------------------------------------------------------------------*/
static int bma255_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id); 
static int bma255_i2c_remove(struct i2c_client *client);
#ifndef CONFIG_HAS_EARLYSUSPEND
static int bma255_suspend(struct i2c_client *client, pm_message_t msg);
static int bma255_resume(struct i2c_client *client);
#endif
#ifdef USE_NEW_SENSOR_ARCH
static int gsensor_local_init(void);
static int gsensor_remove(void);
static int bma255_init_flag =-1; // 0<==>OK -1 <==> fail
//extern struct acc_hw* get_cust_bma255_acc_hw(void);

static struct acc_hw accel_cust;
static struct acc_hw *hw = &accel_cust;



static struct acc_hw *get_cust_acc_hw(void)
{
	return &accel_cust;
}

static struct acc_init_info bma255_init_info = {
    .name = "bma255",
    .init = gsensor_local_init,
    .uninit = gsensor_remove,
};
#endif
/*----------------------------------------------------------------------------*/
typedef enum {
    BMA_TRC_FILTER  = 0x01,
    BMA_TRC_RAWDATA = 0x02,
    BMA_TRC_IOCTL   = 0x04,
    BMA_TRC_CALI	= 0X08,
    BMA_TRC_INFO	= 0X10,
} BMA_TRC;
/*----------------------------------------------------------------------------*/
struct scale_factor{
    u8  whole;
    u8  fraction;
};
/*----------------------------------------------------------------------------*/
struct data_resolution {
    struct scale_factor scalefactor;
    int                 sensitivity;
};
/*----------------------------------------------------------------------------*/
#define C_MAX_FIR_LENGTH (32)
/*----------------------------------------------------------------------------*/
struct data_filter {
    s16 raw[C_MAX_FIR_LENGTH][BMA255_AXES_NUM];
    int sum[BMA255_AXES_NUM];
    int num;
    int idx;
};
/*----------------------------------------------------------------------------*/
struct bma255_i2c_data {
    struct i2c_client *client;
    struct acc_hw *hw;
    struct hwmsen_convert   cvt;
    
    /*misc*/
    struct data_resolution *reso;
    atomic_t                trace;
    atomic_t                suspend;
    atomic_t                selftest;
	atomic_t				filter;
    s16                     cali_sw[BMA255_AXES_NUM+1];
    struct mutex lock;

    /*data*/
    s8                      offset[BMA255_AXES_NUM+1];  /*+1: for 4-byte alignment*/
    s16                     data[BMA255_AXES_NUM+1];
    u8			    fifo_count;

#if defined(CONFIG_BMA255_LOWPASS)
    atomic_t                firlen;
    atomic_t                fir_en;
    struct data_filter      fir;
#endif 
    /*early suspend*/
};

static const struct of_device_id accel_of_match[] = {
	{.compatible = "mediatek,gsensor"},
	{},
};

/*----------------------------------------------------------------------------*/
static struct i2c_driver bma255_i2c_driver = {
    .driver = {
        .name           = BMA255_DEV_NAME,
        .of_match_table = accel_of_match,
    },
	.probe      		= bma255_i2c_probe,
	.remove    			= bma255_i2c_remove,
       .suspend            = bma255_suspend,
       .resume             = bma255_resume,
	.id_table = bma255_i2c_id,
};

/*----------------------------------------------------------------------------*/
static struct i2c_client *bma255_i2c_client;
static struct platform_driver bma255_gsensor_driver;
static struct bma255_i2c_data *obj_i2c_data;
static bool sensor_power = true;
static struct GSENSOR_VECTOR3D gsensor_gain;
static char selftestRes[8]= {0}; 
static struct mutex i2c_lock;
static DEFINE_MUTEX(gsensor_mutex);
static DEFINE_MUTEX(gsensor_scp_en_mutex);

/*----------------------------------------------------------------------------*/
#define GSE_TAG                  "[Gsensor] "
#define GSE_FUN(f)               printk(GSE_TAG"%s\n", __FUNCTION__)
#define GSE_ERR(fmt, args...)    printk(GSE_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define GSE_LOG(fmt, args...)    printk(GSE_TAG fmt, ##args)

#define DMA_BUFFER_SIZE 1024
static unsigned char *I2CDMABuf_va = NULL;
static volatile unsigned int I2CDMABuf_pa = NULL;
/*----------------------------------------------------------------------------*/
static struct data_resolution bma255_data_resolution[1] = {
 /* combination by {FULL_RES,RANGE}*/
    {{ 1, 95}, 512},   // dataformat +/-4g  in 12-bit resolution;  { 1, 95} = 1.95 = (2*4*1000)/(2^12);  512 = (2^12)/(2*4)
};
/*----------------------------------------------------------------------------*/
static struct data_resolution bma255_offset_resolution = {{1, 95}, 512};

static int i2c_dma_read(struct i2c_client *client, unsigned char regaddr, unsigned char *readbuf, int readlen)
{
    	int ret=0;
	if(readlen > DMA_BUFFER_SIZE)
	{
		GSE_ERR("Read length cann't exceed dma buffer size!\n");
		return -EINVAL;
	}
    	mutex_lock(&i2c_lock);		
	//write the register address
	ret= i2c_master_send(client, &regaddr, 1);
	if (ret < 0) {
		GSE_ERR("send command error!!\n");
		return -EFAULT;
	}

	//dma read 
	client->addr = client->addr & I2C_MASK_FLAG | I2C_DMA_FLAG;
	ret = i2c_master_recv(client, (unsigned char *)I2CDMABuf_pa, readlen);
	//clear DMA flag once transfer done
	client->addr = client->addr & I2C_MASK_FLAG & (~ I2C_DMA_FLAG); //client->ext_flag = client->ext_flag & (~I2C_DMA_FLAG);
	if (ret < 0) {
		GSE_ERR("dma receive data error!!\n");
		return -EFAULT;
	}
	memcpy(readbuf, I2CDMABuf_va, readlen);
	mutex_unlock(&i2c_lock);
	return ret;
}

/* I2C operation functions */
static int bma_i2c_read_block(struct i2c_client *client,
			u8 addr, u8 *data, u8 len)
{
	u8 beg = addr;
	int err;
	struct i2c_msg msgs[2] = { {0}, {0} };

	mutex_lock(&gsensor_mutex);

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &beg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data;

	if (!client) {
		mutex_unlock(&gsensor_mutex);
		return -EINVAL;
	} else if (len > C_I2C_FIFO_SIZE) {
		GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		mutex_unlock(&gsensor_mutex);
		return -EINVAL;
	}
	err = i2c_transfer(client->adapter, msgs, sizeof(msgs) / sizeof(msgs[0]));
	if (err != 2) {
		GSE_ERR("i2c_transfer error: (%d %p %d) %d\n", addr, data, len, err);
		err = -EIO;
	} else {
		err = 0;
	}
	mutex_unlock(&gsensor_mutex);
	return err;

}
#define I2C_BUFFER_SIZE 256
static int bma_i2c_write_block(struct i2c_client *client, u8 addr,
			u8 *data, u8 len)
{
#ifdef CONFIG_I2C_BASIC_FUNCTION
	/*
	*because address also occupies one byte,
	*the maximum length for write is 7 bytes
	*/
	int err, idx = 0, num = 0;
	char buf[32];

	if (!client)
		return -EINVAL;
/*
	else if (len > C_I2C_FIFO_SIZE) {
		GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		return -EINVAL;
	}
*/

	buf[num++] = addr;
	for (idx = 0; idx < len; idx++)
		buf[num++] = data[idx];

	mutex_lock(&i2c_lock);
	err = i2c_master_send(client, buf, num);
	mutex_unlock(&i2c_lock);
	if (err < 0) {
		GSE_ERR("send command error!!\n");
		return -EFAULT;
	} else {
		err = 0;/*no error*/
	}
	return err;
#else
	int err = 0;
	err = i2c_smbus_write_i2c_block_data(client, addr, len, data);
	if (err < 0)
		return -1;
	return 0;
#endif
}

/*--------------------BMA255 power control function----------------------------------*/
static void BMA255_power(struct acc_hw *hw, unsigned int on) 
{
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int BMA255_SetDataResolution(struct bma255_i2c_data *obj)
{

/*set g sensor dataresolution here*/

/*BMA255 only can set to 10-bit dataresolution, so do nothing in bma255 driver here*/

/*end of set dataresolution*/
 
 /*we set measure range from -2g to +2g in BMA255_SetDataFormat(client, BMA255_RANGE_2G), 
                                                    and set 10-bit dataresolution BMA255_SetDataResolution()*/
                                                    
 /*so bma255_data_resolution[0] set value as {{ 3, 9}, 256} when declaration, and assign the value to obj->reso here*/  

 	obj->reso = &bma255_data_resolution[0];
	return 0;
	
/*if you changed the measure range, for example call: BMA255_SetDataFormat(client, BMA255_RANGE_4G), 
you must set the right value to bma255_data_resolution*/

}
/*----------------------------------------------------------------------------*/
static int BMA255_ReadData(struct i2c_client *client, s16 data[BMA255_AXES_NUM])
{
	struct bma255_i2c_data *priv = i2c_get_clientdata(client);        
	u8 addr = BMA255_REG_DATAXLOW;
	u8 buf[BMA255_DATA_LEN] = {0};
	int err = 0;

	if(NULL == client)
	{
		err = -EINVAL;
	}
	else if(err = bma_i2c_read_block(client, addr, buf, BMA255_DATA_LEN))
	{
		GSE_ERR("error: %d\n", err);
	}
	else
	{
		/* Convert sensor raw data to 16-bit integer */
		data[BMA255_AXIS_X] = BMA255_GET_BITSLICE(buf[0], BMA255_ACC_X_LSB)
			|(BMA255_GET_BITSLICE(buf[1],
						BMA255_ACC_X_MSB)<<BMA255_ACC_X_LSB__LEN);
		data[BMA255_AXIS_X] = data[BMA255_AXIS_X] << (sizeof(short)*8-(BMA255_ACC_X_LSB__LEN
					+ BMA255_ACC_X_MSB__LEN));
		data[BMA255_AXIS_X] = data[BMA255_AXIS_X] >> (sizeof(short)*8-(BMA255_ACC_X_LSB__LEN
					+ BMA255_ACC_X_MSB__LEN));
		data[BMA255_AXIS_Y] = BMA255_GET_BITSLICE(buf[2], BMA255_ACC_Y_LSB)
			| (BMA255_GET_BITSLICE(buf[3],
						BMA255_ACC_Y_MSB)<<BMA255_ACC_Y_LSB__LEN);
		data[BMA255_AXIS_Y] = data[BMA255_AXIS_Y] << (sizeof(short)*8-(BMA255_ACC_Y_LSB__LEN
					+ BMA255_ACC_Y_MSB__LEN));
		data[BMA255_AXIS_Y] = data[BMA255_AXIS_Y] >> (sizeof(short)*8-(BMA255_ACC_Y_LSB__LEN
					+ BMA255_ACC_Y_MSB__LEN));
		data[BMA255_AXIS_Z] = BMA255_GET_BITSLICE(buf[4], BMA255_ACC_Z_LSB)
			| (BMA255_GET_BITSLICE(buf[5],
						BMA255_ACC_Z_MSB)<<BMA255_ACC_Z_LSB__LEN);
		data[BMA255_AXIS_Z] = data[BMA255_AXIS_Z] << (sizeof(short)*8-(BMA255_ACC_Z_LSB__LEN
					+ BMA255_ACC_Z_MSB__LEN));
		data[BMA255_AXIS_Z] = data[BMA255_AXIS_Z] >> (sizeof(short)*8-(BMA255_ACC_Z_LSB__LEN
					+ BMA255_ACC_Z_MSB__LEN));

#ifdef CONFIG_BMA255_LOWPASS
		if(atomic_read(&priv->filter))
		{
			if(atomic_read(&priv->fir_en) && !atomic_read(&priv->suspend))
			{
				int idx, firlen = atomic_read(&priv->firlen);   
				if(priv->fir.num < firlen)
				{                
					priv->fir.raw[priv->fir.num][BMA255_AXIS_X] = data[BMA255_AXIS_X];
					priv->fir.raw[priv->fir.num][BMA255_AXIS_Y] = data[BMA255_AXIS_Y];
					priv->fir.raw[priv->fir.num][BMA255_AXIS_Z] = data[BMA255_AXIS_Z];
					priv->fir.sum[BMA255_AXIS_X] += data[BMA255_AXIS_X];
					priv->fir.sum[BMA255_AXIS_Y] += data[BMA255_AXIS_Y];
					priv->fir.sum[BMA255_AXIS_Z] += data[BMA255_AXIS_Z];
					if(atomic_read(&priv->trace) & BMA_TRC_FILTER)
					{
						GSE_LOG("add [%2d] [%5d %5d %5d] => [%5d %5d %5d]\n", priv->fir.num,
							priv->fir.raw[priv->fir.num][BMA255_AXIS_X], priv->fir.raw[priv->fir.num][BMA255_AXIS_Y], priv->fir.raw[priv->fir.num][BMA255_AXIS_Z],
							priv->fir.sum[BMA255_AXIS_X], priv->fir.sum[BMA255_AXIS_Y], priv->fir.sum[BMA255_AXIS_Z]);
					}
					priv->fir.num++;
					priv->fir.idx++;
				}
				else
				{
					idx = priv->fir.idx % firlen;
					priv->fir.sum[BMA255_AXIS_X] -= priv->fir.raw[idx][BMA255_AXIS_X];
					priv->fir.sum[BMA255_AXIS_Y] -= priv->fir.raw[idx][BMA255_AXIS_Y];
					priv->fir.sum[BMA255_AXIS_Z] -= priv->fir.raw[idx][BMA255_AXIS_Z];
					priv->fir.raw[idx][BMA255_AXIS_X] = data[BMA255_AXIS_X];
					priv->fir.raw[idx][BMA255_AXIS_Y] = data[BMA255_AXIS_Y];
					priv->fir.raw[idx][BMA255_AXIS_Z] = data[BMA255_AXIS_Z];
					priv->fir.sum[BMA255_AXIS_X] += data[BMA255_AXIS_X];
					priv->fir.sum[BMA255_AXIS_Y] += data[BMA255_AXIS_Y];
					priv->fir.sum[BMA255_AXIS_Z] += data[BMA255_AXIS_Z];
					priv->fir.idx++;
					data[BMA255_AXIS_X] = priv->fir.sum[BMA255_AXIS_X]/firlen;
					data[BMA255_AXIS_Y] = priv->fir.sum[BMA255_AXIS_Y]/firlen;
					data[BMA255_AXIS_Z] = priv->fir.sum[BMA255_AXIS_Z]/firlen;
					if(atomic_read(&priv->trace) & BMA_TRC_FILTER)
					{
						GSE_LOG("add [%2d] [%5d %5d %5d] => [%5d %5d %5d] : [%5d %5d %5d]\n", idx,
						priv->fir.raw[idx][BMA255_AXIS_X], priv->fir.raw[idx][BMA255_AXIS_Y], priv->fir.raw[idx][BMA255_AXIS_Z],
						priv->fir.sum[BMA255_AXIS_X], priv->fir.sum[BMA255_AXIS_Y], priv->fir.sum[BMA255_AXIS_Z],
						data[BMA255_AXIS_X], data[BMA255_AXIS_Y], data[BMA255_AXIS_Z]);
					}
				}
			}
		}	
#endif         
	}
	return err;
}
/*----------------------------------------------------------------------------*/
static int BMA255_ReadOffset(struct i2c_client *client, s8 ofs[BMA255_AXES_NUM])
{    
	int err;
#ifdef SW_CALIBRATION
	ofs[0]=ofs[1]=ofs[2]=0x0;
#else
	if(err = bma_i2c_read_block(client, BMA255_REG_OFSX, ofs, BMA255_AXES_NUM))
	{
		GSE_ERR("error: %d\n", err);
	}
#endif
	//printk("offesx=%x, y=%x, z=%x",ofs[0],ofs[1],ofs[2]);
	
	return err;    
}
/*----------------------------------------------------------------------------*/
static int BMA255_ResetCalibration(struct i2c_client *client)
{
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	u8 ofs[4]={0,0,0,0};
	int err;
	
	#ifdef SW_CALIBRATION
		
	#else
		if(err = bma_i2c_write_block(client, BMA255_REG_OFSX, ofs, 4))
		{
			GSE_ERR("error: %d\n", err);
		}
	#endif

	memset(obj->cali_sw, 0x00, sizeof(obj->cali_sw));
	memset(obj->offset, 0x00, sizeof(obj->offset));
	return err;    
}
/*----------------------------------------------------------------------------*/
static int BMA255_ReadCalibration(struct i2c_client *client, int dat[BMA255_AXES_NUM])
{
    struct bma255_i2c_data *obj = i2c_get_clientdata(client);
    int err = 0;
    int mul;

	#ifdef SW_CALIBRATION
		mul = 0;//only SW Calibration, disable HW Calibration
	#else
	    if ((err = BMA255_ReadOffset(client, obj->offset))) {
        GSE_ERR("read offset fail, %d\n", err);
        return err;
    	}    
    	mul = obj->reso->sensitivity/bma255_offset_resolution.sensitivity;
	#endif

    dat[obj->cvt.map[BMA255_AXIS_X]] = obj->cvt.sign[BMA255_AXIS_X]*(obj->offset[BMA255_AXIS_X]*mul + obj->cali_sw[BMA255_AXIS_X]);
    dat[obj->cvt.map[BMA255_AXIS_Y]] = obj->cvt.sign[BMA255_AXIS_Y]*(obj->offset[BMA255_AXIS_Y]*mul + obj->cali_sw[BMA255_AXIS_Y]);
    dat[obj->cvt.map[BMA255_AXIS_Z]] = obj->cvt.sign[BMA255_AXIS_Z]*(obj->offset[BMA255_AXIS_Z]*mul + obj->cali_sw[BMA255_AXIS_Z]);                        
                                       
    return err;
}
/*----------------------------------------------------------------------------*/
static int BMA255_ReadCalibrationEx(struct i2c_client *client, int act[BMA255_AXES_NUM], int raw[BMA255_AXES_NUM])
{  
	/*raw: the raw calibration data; act: the actual calibration data*/
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	int err;
	int mul;

	#ifdef SW_CALIBRATION
		mul = 0;//only SW Calibration, disable HW Calibration
	#else
		if(err = BMA255_ReadOffset(client, obj->offset))
		{
			GSE_ERR("read offset fail, %d\n", err);
			return err;
		}   
		mul = obj->reso->sensitivity/bma255_offset_resolution.sensitivity;
	#endif
	
	raw[BMA255_AXIS_X] = obj->offset[BMA255_AXIS_X]*mul + obj->cali_sw[BMA255_AXIS_X];
	raw[BMA255_AXIS_Y] = obj->offset[BMA255_AXIS_Y]*mul + obj->cali_sw[BMA255_AXIS_Y];
	raw[BMA255_AXIS_Z] = obj->offset[BMA255_AXIS_Z]*mul + obj->cali_sw[BMA255_AXIS_Z];

	act[obj->cvt.map[BMA255_AXIS_X]] = obj->cvt.sign[BMA255_AXIS_X]*raw[BMA255_AXIS_X];
	act[obj->cvt.map[BMA255_AXIS_Y]] = obj->cvt.sign[BMA255_AXIS_Y]*raw[BMA255_AXIS_Y];
	act[obj->cvt.map[BMA255_AXIS_Z]] = obj->cvt.sign[BMA255_AXIS_Z]*raw[BMA255_AXIS_Z];                        
	                       
	return 0;
}
/*----------------------------------------------------------------------------*/
static int BMA255_WriteCalibration(struct i2c_client *client, int dat[BMA255_AXES_NUM])
{
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	int err = 0;
	int cali[BMA255_AXES_NUM], raw[BMA255_AXES_NUM];
	int lsb = bma255_offset_resolution.sensitivity;
	int divisor = obj->reso->sensitivity/lsb;

	if(err = BMA255_ReadCalibrationEx(client, cali, raw))	/*offset will be updated in obj->offset*/
	{ 
		GSE_ERR("read offset fail, %d\n", err);
		return err;
	}

	GSE_LOG("OLDOFF: (%+3d %+3d %+3d): (%+3d %+3d %+3d) / (%+3d %+3d %+3d)\n", 
		raw[BMA255_AXIS_X], raw[BMA255_AXIS_Y], raw[BMA255_AXIS_Z],
		obj->offset[BMA255_AXIS_X], obj->offset[BMA255_AXIS_Y], obj->offset[BMA255_AXIS_Z],
		obj->cali_sw[BMA255_AXIS_X], obj->cali_sw[BMA255_AXIS_Y], obj->cali_sw[BMA255_AXIS_Z]);

	/*calculate the real offset expected by caller*/
	cali[BMA255_AXIS_X] += dat[BMA255_AXIS_X];
	cali[BMA255_AXIS_Y] += dat[BMA255_AXIS_Y];
	cali[BMA255_AXIS_Z] += dat[BMA255_AXIS_Z];

	GSE_LOG("UPDATE: (%+3d %+3d %+3d)\n", 
		dat[BMA255_AXIS_X], dat[BMA255_AXIS_Y], dat[BMA255_AXIS_Z]);

#ifdef SW_CALIBRATION
	obj->cali_sw[BMA255_AXIS_X] = obj->cvt.sign[BMA255_AXIS_X]*(cali[obj->cvt.map[BMA255_AXIS_X]]);
	obj->cali_sw[BMA255_AXIS_Y] = obj->cvt.sign[BMA255_AXIS_Y]*(cali[obj->cvt.map[BMA255_AXIS_Y]]);
	obj->cali_sw[BMA255_AXIS_Z] = obj->cvt.sign[BMA255_AXIS_Z]*(cali[obj->cvt.map[BMA255_AXIS_Z]]);	
#else
	obj->offset[BMA255_AXIS_X] = (s8)(obj->cvt.sign[BMA255_AXIS_X]*(cali[obj->cvt.map[BMA255_AXIS_X]])/(divisor));
	obj->offset[BMA255_AXIS_Y] = (s8)(obj->cvt.sign[BMA255_AXIS_Y]*(cali[obj->cvt.map[BMA255_AXIS_Y]])/(divisor));
	obj->offset[BMA255_AXIS_Z] = (s8)(obj->cvt.sign[BMA255_AXIS_Z]*(cali[obj->cvt.map[BMA255_AXIS_Z]])/(divisor));

	/*convert software calibration using standard calibration*/
	obj->cali_sw[BMA255_AXIS_X] = obj->cvt.sign[BMA255_AXIS_X]*(cali[obj->cvt.map[BMA255_AXIS_X]])%(divisor);
	obj->cali_sw[BMA255_AXIS_Y] = obj->cvt.sign[BMA255_AXIS_Y]*(cali[obj->cvt.map[BMA255_AXIS_Y]])%(divisor);
	obj->cali_sw[BMA255_AXIS_Z] = obj->cvt.sign[BMA255_AXIS_Z]*(cali[obj->cvt.map[BMA255_AXIS_Z]])%(divisor);

	GSE_LOG("NEWOFF: (%+3d %+3d %+3d): (%+3d %+3d %+3d) / (%+3d %+3d %+3d)\n", 
		obj->offset[BMA255_AXIS_X]*divisor + obj->cali_sw[BMA255_AXIS_X], 
		obj->offset[BMA255_AXIS_Y]*divisor + obj->cali_sw[BMA255_AXIS_Y], 
		obj->offset[BMA255_AXIS_Z]*divisor + obj->cali_sw[BMA255_AXIS_Z], 
		obj->offset[BMA255_AXIS_X], obj->offset[BMA255_AXIS_Y], obj->offset[BMA255_AXIS_Z],
		obj->cali_sw[BMA255_AXIS_X], obj->cali_sw[BMA255_AXIS_Y], obj->cali_sw[BMA255_AXIS_Z]);

	if(err = bma_i2c_write_block(obj->client, BMA255_REG_OFSX, obj->offset, BMA255_AXES_NUM))
	{
		GSE_ERR("write offset fail: %d\n", err);
		return err;
	}
#endif

	return err;
}
/*----------------------------------------------------------------------------*/
static int BMA255_CheckDeviceID(struct i2c_client *client)
{
	u8 databuf[2];    
	int res = 0;

	memset(databuf, 0, sizeof(u8)*2);    

	printk("BMA255_CheckDeviceID client.addr=%x\n",client->addr);
	res = bma_i2c_read_block(client, BMA255_REG_DEVID, databuf, 0x01);
	res = bma_i2c_read_block(client, BMA255_REG_DEVID, databuf, 0x01);
	if(res < 0)
		goto exit_BMA255_CheckDeviceID;

	if(databuf[0]!=BMA255_FIXED_DEVID)
	{
		printk("BMA255_CheckDeviceID %d failt!\n ", databuf[0]);
		return BMA255_ERR_IDENTIFICATION;
	}
	else
	{
		printk("BMA255_CheckDeviceID %d pass!\n ", databuf[0]);
	}

	exit_BMA255_CheckDeviceID:
	if (res < 0)
	{
		return BMA255_ERR_I2C;
	}
	
	return BMA255_SUCCESS;
}
/*----------------------------------------------------------------------------*/
static int BMA255_SetPowerMode(struct i2c_client *client, bool enable)
{
	u8 databuf[2] = {0};    
	int res = 0;
	u8 addr = BMA255_REG_POWER_CTL;
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	u8 actual_power_mode = 0;
	
	if(enable == sensor_power )
	{
		GSE_LOG("Sensor power status is newest!\n");
		return BMA255_SUCCESS;
	}
	
	mutex_lock(&obj->lock);
	if(enable)
	{
		actual_power_mode = BMA255_MODE_NORMAL;
	}
	else
	{
		actual_power_mode = BMA255_MODE_SUSPEND;
	}
	
	res = bma_i2c_read_block(client,
			BMA255_MODE_CTRL_REG, &databuf[0], 1);
	res += bma_i2c_read_block(client,
		BMA255_LOW_POWER_CTRL_REG, &databuf[1], 1);

	switch (actual_power_mode) {
	case BMA255_MODE_NORMAL:
		databuf[0] = BMA255_SET_BITSLICE(databuf[0],
			BMA255_MODE_CTRL, 0);
		databuf[1] = BMA255_SET_BITSLICE(databuf[1],
			BMA255_LOW_POWER_MODE, 0);
		res += bma_i2c_write_block(client,
			BMA255_MODE_CTRL_REG, &databuf[0], 1);
		mdelay(1);
		res += bma_i2c_write_block(client,
			BMA255_LOW_POWER_CTRL_REG, &databuf[1], 1);
		mdelay(1);
	break;
	case BMA255_MODE_SUSPEND:
		databuf[0] = BMA255_SET_BITSLICE(databuf[0],
			BMA255_MODE_CTRL, 4);
		databuf[1] = BMA255_SET_BITSLICE(databuf[1],
			BMA255_LOW_POWER_MODE, 0);
		res += bma_i2c_write_block(client,
			BMA255_LOW_POWER_CTRL_REG, &databuf[1], 1);
		mdelay(1);
		res += bma_i2c_write_block(client,
			BMA255_MODE_CTRL_REG, &databuf[0], 1);
		mdelay(1);
	break;
	}

	if(res < 0)
	{
		GSE_ERR("set power mode failed, res = %d\n", res);
		mutex_unlock(&obj->lock);
		return BMA255_ERR_I2C;
	}
	sensor_power = enable;
	mutex_unlock(&obj->lock);
	
	return BMA255_SUCCESS;    
}
/*----------------------------------------------------------------------------*/
static int BMA255_SetDataFormat(struct i2c_client *client, u8 dataformat)
{
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	u8 databuf[2] = {0};    
	int res = 0;

	mutex_lock(&obj->lock);
	res = bma_i2c_read_block(client,
		BMA255_RANGE_SEL_REG, &databuf[0], 1);
	databuf[0] = BMA255_SET_BITSLICE(databuf[0],
		BMA255_RANGE_SEL, dataformat);
	res += bma_i2c_write_block(client,
		BMA255_RANGE_SEL_REG, &databuf[0], 1);
	mdelay(1);

	if(res < 0)
	{
		GSE_ERR("set data format failed, res = %d\n", res);
		mutex_unlock(&obj->lock);
		return BMA255_ERR_I2C;
	}
	mutex_unlock(&obj->lock);
	
	return BMA255_SetDataResolution(obj);    
}
/*----------------------------------------------------------------------------*/
static int BMA255_SetBWRate(struct i2c_client *client, u8 bwrate)
{
	u8 databuf[2] = {0};    
	int res = 0;
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);

	mutex_lock(&obj->lock);
	res = bma_i2c_read_block(client,
		BMA255_BANDWIDTH__REG, &databuf[0], 1);
	databuf[0] = BMA255_SET_BITSLICE(databuf[0],
		BMA255_BANDWIDTH, bwrate);
	res += bma_i2c_write_block(client,
		BMA255_BANDWIDTH__REG, &databuf[0], 1);
	mdelay(1);

	if(res < 0)
	{
		GSE_ERR("set bandwidth failed, res = %d\n", res);
		mutex_unlock(&obj->lock);
		return BMA255_ERR_I2C;
	}
	mutex_unlock(&obj->lock);

	return BMA255_SUCCESS;    
}
/*----------------------------------------------------------------------------*/
static int BMA255_SetIntEnable(struct i2c_client *client, u8 intenable)
{
	int res = 0;
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	
	mutex_lock(&obj->lock);
	res = bma_i2c_write_block(client, BMA255_INT_REG_1, &intenable, 0x01);
	mdelay(1);
	if(res != BMA255_SUCCESS) 
	{
		mutex_unlock(&obj->lock);
		return res;
	}

	res = bma_i2c_write_block(client, BMA255_INT_REG_2, &intenable, 0x01);
	mdelay(1);
	if(res != BMA255_SUCCESS) 
	{
		mutex_unlock(&obj->lock);
		return res;
	}
	mutex_unlock(&obj->lock);
	printk("BMA255 disable interrupt ...\n");

	/*for disable interrupt function*/

	return BMA255_SUCCESS;	  
}

/*----------------------------------------------------------------------------*/
static int bma255_init_client(struct i2c_client *client, int reset_cali)
{
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	int res = 0;
	printk("bma255_init_client \n");

	res = BMA255_CheckDeviceID(client); 
	if(res != BMA255_SUCCESS)
	{
		return res;
	}	
	printk("BMA255_CheckDeviceID ok \n");
	
	res = BMA255_SetBWRate(client, BMA255_BW_100HZ);
	if(res != BMA255_SUCCESS ) 
	{
		return res;
	}
	printk("BMA255_SetBWRate OK!\n");
	
	res = BMA255_SetDataFormat(client, BMA255_RANGE_4G);
	if(res != BMA255_SUCCESS) 
	{
		return res;
	}
	printk("BMA255_SetDataFormat OK!\n");

	gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = obj->reso->sensitivity;

	res = BMA255_SetIntEnable(client, 0x00);        
	if(res != BMA255_SUCCESS)
	{
		return res;
	}
	printk("BMA255 disable interrupt function!\n");

	res = BMA255_SetPowerMode(client, false);
	if(res != BMA255_SUCCESS)
	{
		return res;
	}
	printk("BMA255_SetPowerMode OK!\n");

	if(0 != reset_cali)
	{ 
		/*reset calibration only in power on*/
		res = BMA255_ResetCalibration(client);
		if(res != BMA255_SUCCESS)
		{
			return res;
		}
	}
	printk("bma255_init_client OK!\n");
#ifdef CONFIG_BMA255_LOWPASS
	memset(&obj->fir, 0x00, sizeof(obj->fir));  
#endif

	mdelay(20);

	return BMA255_SUCCESS;
}
/*----------------------------------------------------------------------------*/
static int BMA255_ReadChipInfo(struct i2c_client *client, char *buf, int bufsize)
{
	u8 databuf[10];    

	memset(databuf, 0, sizeof(u8)*10);

	if((NULL == buf)||(bufsize<=30))
	{
		return -1;
	}
	
	if(NULL == client)
	{
		*buf = 0;
		return -2;
	}

	sprintf(buf, "BMC156 Chip");
	return 0;
}
/*----------------------------------------------------------------------------*/
static int BMA255_CompassReadData(struct i2c_client *client, char *buf, int bufsize)
{
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);
	//u8 databuf[20];
	int acc[BMA255_AXES_NUM];
	int res = 0;
	s16 databuf[BMA255_AXES_NUM];
	//memset(databuf, 0, sizeof(u8)*10);

	if(NULL == buf)
	{
		return -1;
	}
	if(NULL == client)
	{
		*buf = 0;
		return -2;
	}

	if(!sensor_power)
	{
		res = BMA255_SetPowerMode(client, true);
		if(res)
		{
			GSE_ERR("Power on bma255 error %d!\n", res);
		}
	}

	if(res = BMA255_ReadData(client, databuf))
	{        
		GSE_ERR("I2C error: ret value=%d", res);
		return -3;
	}
	else
	{
		/*remap coordinate*/
		acc[obj->cvt.map[BMA255_AXIS_X]] = obj->cvt.sign[BMA255_AXIS_X]*databuf[BMA255_AXIS_X];
		acc[obj->cvt.map[BMA255_AXIS_Y]] = obj->cvt.sign[BMA255_AXIS_Y]*databuf[BMA255_AXIS_Y];
		acc[obj->cvt.map[BMA255_AXIS_Z]] = obj->cvt.sign[BMA255_AXIS_Z]*databuf[BMA255_AXIS_Z];
		//printk("cvt x=%d, y=%d, z=%d \n",obj->cvt.sign[BMA255_AXIS_X],obj->cvt.sign[BMA255_AXIS_Y],obj->cvt.sign[BMA255_AXIS_Z]);

		//GSE_LOG("Mapped gsensor data: %d, %d, %d!\n", acc[BMA255_AXIS_X], acc[BMA255_AXIS_Y], acc[BMA255_AXIS_Z]);

		sprintf(buf, "%d %d %d", (s16)acc[BMA255_AXIS_X], (s16)acc[BMA255_AXIS_Y], (s16)acc[BMA255_AXIS_Z]);
		if(atomic_read(&obj->trace) & BMA_TRC_IOCTL)
		{
			GSE_LOG("gsensor data for compass: %s!\n", buf);
		}
	}
	
	return 0;
}
/*----------------------------------------------------------------------------*/
static int BMA255_ReadSensorData(struct i2c_client *client, char *buf, int bufsize)
{
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);
	//u8 databuf[20];
	int acc[BMA255_AXES_NUM];
	int res = 0;
	s16 databuf[BMA255_AXES_NUM];
	//memset(databuf, 0, sizeof(u8)*10);

	if(NULL == buf)
	{
		return -1;
	}
	if(NULL == client)
	{
		*buf = 0;
		return -2;
	}

	if(!sensor_power)
	{
		res = BMA255_SetPowerMode(client, true);
		if(res)
		{
			GSE_ERR("Power on bma255 error %d!\n", res);
		}
	}

	if(res = BMA255_ReadData(client, databuf))
	{        
		GSE_ERR("I2C error: ret value=%d", res);
		return -3;
	}
	else
	{
		//printk("raw data x=%d, y=%d, z=%d \n",obj->data[BMA255_AXIS_X],obj->data[BMA255_AXIS_Y],obj->data[BMA255_AXIS_Z]);
		databuf[BMA255_AXIS_X] += obj->cali_sw[BMA255_AXIS_X];
		databuf[BMA255_AXIS_Y] += obj->cali_sw[BMA255_AXIS_Y];
		databuf[BMA255_AXIS_Z] += obj->cali_sw[BMA255_AXIS_Z];
		
		//printk("cali_sw x=%d, y=%d, z=%d \n",obj->cali_sw[BMA255_AXIS_X],obj->cali_sw[BMA255_AXIS_Y],obj->cali_sw[BMA255_AXIS_Z]);
		
		/*remap coordinate*/
		acc[obj->cvt.map[BMA255_AXIS_X]] = obj->cvt.sign[BMA255_AXIS_X]*databuf[BMA255_AXIS_X];
		acc[obj->cvt.map[BMA255_AXIS_Y]] = obj->cvt.sign[BMA255_AXIS_Y]*databuf[BMA255_AXIS_Y];
		acc[obj->cvt.map[BMA255_AXIS_Z]] = obj->cvt.sign[BMA255_AXIS_Z]*databuf[BMA255_AXIS_Z];
		//printk("cvt x=%d, y=%d, z=%d \n",obj->cvt.sign[BMA255_AXIS_X],obj->cvt.sign[BMA255_AXIS_Y],obj->cvt.sign[BMA255_AXIS_Z]);

		//GSE_LOG("Mapped gsensor data: %d, %d, %d!\n", acc[BMA255_AXIS_X], acc[BMA255_AXIS_Y], acc[BMA255_AXIS_Z]);

		//Out put the mg
		//printk("mg acc=%d, GRAVITY=%d, sensityvity=%d \n",acc[BMA255_AXIS_X],GRAVITY_EARTH_1000,obj->reso->sensitivity);
		acc[BMA255_AXIS_X] = acc[BMA255_AXIS_X] * GRAVITY_EARTH_1000 / obj->reso->sensitivity;
		acc[BMA255_AXIS_Y] = acc[BMA255_AXIS_Y] * GRAVITY_EARTH_1000 / obj->reso->sensitivity;
		acc[BMA255_AXIS_Z] = acc[BMA255_AXIS_Z] * GRAVITY_EARTH_1000 / obj->reso->sensitivity;		

		sprintf(buf, "%04x %04x %04x", acc[BMA255_AXIS_X], acc[BMA255_AXIS_Y], acc[BMA255_AXIS_Z]);
		if(atomic_read(&obj->trace) & BMA_TRC_IOCTL)
		{
			GSE_LOG("gsensor data: %s!\n", buf);
		}
	}
	
	return 0;
}
/*----------------------------------------------------------------------------*/
static int BMA255_ReadRawData(struct i2c_client *client, char *buf)
{
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);
	int res = 0;
	s16 databuf[BMA255_AXES_NUM];

	if (!buf || !client)
	{
		return EINVAL;
	}
	
	if(res = BMA255_ReadData(client, databuf))
	{        
		GSE_ERR("I2C error: ret value=%d", res);
		return EIO;
	}
	else
	{
		sprintf(buf, "BMA255_ReadRawData %04x %04x %04x", databuf[BMA255_AXIS_X], 
			databuf[BMA255_AXIS_Y], databuf[BMA255_AXIS_Z]);
	}
	
	return 0;
}
/*----------------------------------------------------------------------------*/
static int bma255_set_mode(struct i2c_client *client, unsigned char mode)
{
	int comres = 0;
	unsigned char data[2] = {0};
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);

	if ((client == NULL) || (mode >= 3))
	{
		return -1;
	}
	mutex_lock(&obj->lock);
	comres = bma_i2c_read_block(client,
			BMA255_EN_LOW_POWER__REG, &data[0], 1);
	comres += bma_i2c_read_block(client,
		BMA255_LOW_POWER_CTRL_REG, &data[1], 1);
	switch (mode) {
	case BMA255_MODE_NORMAL:
		data[0]  = BMA255_SET_BITSLICE(data[0],
				BMA255_MODE_CTRL, 0);
		data[1]  = BMA255_SET_BITSLICE(data[1],
				BMA255_LOW_POWER_MODE, 0);
		comres += bma_i2c_write_block(client,
				BMA255_MODE_CTRL_REG, &data[0], 0x01);
		mdelay(1);
		comres += bma_i2c_write_block(client,
			BMA255_LOW_POWER_CTRL_REG, &data[1], 0x01);
		break;
	case BMA255_MODE_LOWPOWER:
		data[0]  = BMA255_SET_BITSLICE(data[0],
				BMA255_MODE_CTRL, 2);
		data[1]  = BMA255_SET_BITSLICE(data[1],
				BMA255_LOW_POWER_MODE, 0);
		comres += bma_i2c_write_block(client,
				BMA255_MODE_CTRL_REG, &data[0], 0x01);
		mdelay(1);
		comres += bma_i2c_write_block(client,
			BMA255_LOW_POWER_CTRL_REG, &data[1], 0x01);
		break;
	case BMA255_MODE_SUSPEND:
		data[0]  = BMA255_SET_BITSLICE(data[0],
				BMA255_MODE_CTRL, 4);
		data[1]  = BMA255_SET_BITSLICE(data[1],
				BMA255_LOW_POWER_MODE, 0);
		comres += bma_i2c_write_block(client,
			BMA255_LOW_POWER_CTRL_REG, &data[1], 0x01);
		mdelay(1);
		comres += bma_i2c_write_block(client,
			BMA255_MODE_CTRL_REG, &data[0], 0x01);
		break;
	default:
		break;
	}

	mutex_unlock(&obj->lock);

	if(comres <= 0)
	{
		return BMA255_ERR_I2C;
	}
	else
	{
		return comres;
	}
}
/*----------------------------------------------------------------------------*/
static int bma255_get_mode(struct i2c_client *client, unsigned char *mode)
{
	int comres = 0;

	if (client == NULL) 
	{
		return -1;
	}
	comres = bma_i2c_read_block(client,
			BMA255_EN_LOW_POWER__REG, mode, 1);
	*mode  = (*mode) >> 6;
		
	return comres;
}

/*----------------------------------------------------------------------------*/
static int bma255_set_range(struct i2c_client *client, unsigned char range)
{
	int comres = 0;
	unsigned char data[2] = {BMA255_RANGE_SEL__REG};
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);

	if (client == NULL)
	{
		return -1;
	}
	mutex_lock(&obj->lock);
	comres = bma_i2c_read_block(client,
			BMA255_RANGE_SEL__REG, data+1, 1);

	data[1]  = BMA255_SET_BITSLICE(data[1],
			BMA255_RANGE_SEL, range);

	comres = i2c_master_send(client, data, 2);
	mutex_unlock(&obj->lock);

	BMA255_SetDataResolution(obj);

	if(comres <= 0)
	{
		return BMA255_ERR_I2C;
	}
	else
	{
		return comres;
	}
}
/*----------------------------------------------------------------------------*/
static int bma255_get_range(struct i2c_client *client, unsigned char *range)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) 
	{
		return -1;
	}

	comres = bma_i2c_read_block(client, BMA255_RANGE_SEL__REG,	&data, 1);
	*range = BMA255_GET_BITSLICE(data, BMA255_RANGE_SEL);

	return comres;
}
/*----------------------------------------------------------------------------*/
static int bma255_set_bandwidth(struct i2c_client *client, unsigned char bandwidth)
{
	int comres = 0;
	unsigned char data[2] = {BMA255_BANDWIDTH__REG};
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);

	if (client == NULL)
	{
		return -1;
	}

	mutex_lock(&obj->lock);
	comres = bma_i2c_read_block(client,
			BMA255_BANDWIDTH__REG, data+1, 1);

	data[1]  = BMA255_SET_BITSLICE(data[1],
			BMA255_BANDWIDTH, bandwidth);

	comres = i2c_master_send(client, data, 2);
	mutex_unlock(&obj->lock);
	if(comres <= 0)
	{
		return BMA255_ERR_I2C;
	}
	else
	{
		return comres;
	}
}
/*----------------------------------------------------------------------------*/
static int bma255_get_bandwidth(struct i2c_client *client, unsigned char *bandwidth)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) 
	{
		return -1;
	}

	comres = bma_i2c_read_block(client, BMA255_BANDWIDTH__REG, &data, 1);
	data = BMA255_GET_BITSLICE(data, BMA255_BANDWIDTH);

	if (data < 0x08) //7.81Hz
	{
		*bandwidth = 0x08;
	}
	else if (data > 0x0f)	// 1000Hz
	{
		*bandwidth = 0x0f;
	}
	else
	{
		*bandwidth = data;
	}
	return comres;
}

/*----------------------------------------------------------------------------*/
static int bma255_set_fifo_mode(struct i2c_client *client, unsigned char fifo_mode)
{
	int comres = 0;
	unsigned char data[2] = {BMA255_FIFO_MODE__REG};
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);

	if (client == NULL || fifo_mode >= 4)
	{
		return -1;
	}

	mutex_lock(&obj->lock);
	comres = bma_i2c_read_block(client,
			BMA255_FIFO_MODE__REG, data+1, 1);

	data[1]  = BMA255_SET_BITSLICE(data[1],
			BMA255_FIFO_MODE, fifo_mode);

	comres = i2c_master_send(client, data, 2);
	mutex_unlock(&obj->lock);
	if(comres <= 0)
	{
		return BMA255_ERR_I2C;
	}
	else
	{
		return comres;
	}
}
/*----------------------------------------------------------------------------*/
static int bma255_get_fifo_mode(struct i2c_client *client, unsigned char *fifo_mode)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) 
	{
		return -1;
	}

	comres = bma_i2c_read_block(client, BMA255_FIFO_MODE__REG, &data, 1);
	*fifo_mode = BMA255_GET_BITSLICE(data, BMA255_FIFO_MODE);

	return comres;
}

static int bma255_get_fifo_framecount(struct i2c_client *client, unsigned char *framecount)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) 
	{
		return -1;
	}

	comres = bma_i2c_read_block(client, BMA255_FIFO_FRAME_COUNTER_S__REG, &data, 1);
	*framecount = BMA255_GET_BITSLICE(data, BMA255_FIFO_FRAME_COUNTER_S);
	return comres;
}
/*----------------------------------------------------------------------------*/

static ssize_t show_chipinfo_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = bma255_i2c_client;
	char strbuf[BMA255_BUFSIZE];
	if(NULL == client)
	{
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}
	
	BMA255_ReadChipInfo(client, strbuf, BMA255_BUFSIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);        
}

static ssize_t gsensor_init(struct device_driver *ddri, char *buf, size_t count)
{
	struct i2c_client *client = bma255_i2c_client;
	char strbuf[BMA255_BUFSIZE];
	
	if(NULL == client)
	{
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}
	bma255_init_client(client, 1);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);			
}

/*----------------------------------------------------------------------------*/
/*
g sensor opmode for compass tilt compensation
*/
static ssize_t show_cpsopmode_value(struct device_driver *ddri, char *buf)
{
	unsigned char data;

	if (bma255_get_mode(bma255_i2c_client, &data) < 0)
	{
		return sprintf(buf, "Read error\n");
	}
	else
	{
		return sprintf(buf, "%d\n", data);
	}
}

/*----------------------------------------------------------------------------*/
/*
g sensor opmode for compass tilt compensation
*/
static ssize_t store_cpsopmode_value(struct device_driver *ddri, char *buf, size_t count)
{
	unsigned long data;
	int error;

	if (error = kstrtoul(buf, 10, &data))
	{
		return error;
	}
	if (data == BMA255_MODE_NORMAL)
	{
		BMA255_SetPowerMode(bma255_i2c_client, true);
	}
	else if (data == BMA255_MODE_SUSPEND)
	{
		BMA255_SetPowerMode(bma255_i2c_client, false);
	}
	else if (bma255_set_mode(bma255_i2c_client, (unsigned char) data) < 0)
	{
		GSE_ERR("invalid content: '%s', length = %d\n", buf, count);
	}

	return count;
}

/*----------------------------------------------------------------------------*/
/*
g sensor range for compass tilt compensation
*/
static ssize_t show_cpsrange_value(struct device_driver *ddri, char *buf)
{
	unsigned char data;

	if (bma255_get_range(bma255_i2c_client, &data) < 0)
	{
		return sprintf(buf, "Read error\n");
	}
	else
	{
		return sprintf(buf, "%d\n", data);
	}
}

/*----------------------------------------------------------------------------*/
/*
g sensor range for compass tilt compensation
*/
static ssize_t store_cpsrange_value(struct device_driver *ddri, char *buf, size_t count)
{
	unsigned long data;
	int error;

	if (error = kstrtoul(buf, 10, &data))
	{
		return error;
	}
	if (bma255_set_range(bma255_i2c_client, (unsigned char) data) < 0)
	{
		GSE_ERR("invalid content: '%s', length = %d\n", buf, count);
	}

	return count;
}
/*----------------------------------------------------------------------------*/
/*
g sensor bandwidth for compass tilt compensation
*/
static ssize_t show_cpsbandwidth_value(struct device_driver *ddri, char *buf)
{
	unsigned char data;

	if (bma255_get_bandwidth(bma255_i2c_client, &data) < 0)
	{
		return sprintf(buf, "Read error\n");
	}
	else
	{
		return sprintf(buf, "%d\n", data);
	}
}

/*----------------------------------------------------------------------------*/
/*
g sensor bandwidth for compass tilt compensation
*/
static ssize_t store_cpsbandwidth_value(struct device_driver *ddri, char *buf, size_t count)
{
	unsigned long data;
	int error;

	if (error = kstrtoul(buf, 10, &data))
	{
		return error;
	}
	if (bma255_set_bandwidth(bma255_i2c_client, (unsigned char) data) < 0)
	{
		GSE_ERR("invalid content: '%s', length = %d\n", buf, count);
	}

	return count;
}

/*----------------------------------------------------------------------------*/
/*
g sensor data for compass tilt compensation
*/
static ssize_t show_cpsdata_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = bma255_i2c_client;
	char strbuf[BMA255_BUFSIZE];
	
	if(NULL == client)
	{
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}
	BMA255_CompassReadData(client, strbuf, BMA255_BUFSIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);            
}

/*----------------------------------------------------------------------------*/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = bma255_i2c_client;
	char strbuf[BMA255_BUFSIZE];
	
	if(NULL == client)
	{
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}
	BMA255_ReadSensorData(client, strbuf, BMA255_BUFSIZE);
	//BMA255_ReadRawData(client, strbuf);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);            
}

static ssize_t show_sensorrawdata_value(struct device_driver *ddri, char *buf, size_t count)
{
	struct i2c_client *client = bma255_i2c_client;
	char strbuf[BMA255_BUFSIZE];
	
	if(NULL == client)
	{
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}
	//BMA255_ReadSensorData(client, strbuf, BMA255_BUFSIZE);
	BMA255_ReadRawData(client, strbuf);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);			
}

/*----------------------------------------------------------------------------*/
static ssize_t show_cali_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = bma255_i2c_client;
	struct bma255_i2c_data *obj;
	int err, len = 0, mul;
	int tmp[BMA255_AXES_NUM];

	if(NULL == client)
	{
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}

	obj = i2c_get_clientdata(client);

	if(err = BMA255_ReadOffset(client, obj->offset))
	{
		return -EINVAL;
	}
	else if(err = BMA255_ReadCalibration(client, tmp))
	{
		return -EINVAL;
	}
	else
	{    
		mul = obj->reso->sensitivity/bma255_offset_resolution.sensitivity;
		len += snprintf(buf+len, PAGE_SIZE-len, "[HW ][%d] (%+3d, %+3d, %+3d) : (0x%02X, 0x%02X, 0x%02X)\n", mul,                        
			obj->offset[BMA255_AXIS_X], obj->offset[BMA255_AXIS_Y], obj->offset[BMA255_AXIS_Z],
			obj->offset[BMA255_AXIS_X], obj->offset[BMA255_AXIS_Y], obj->offset[BMA255_AXIS_Z]);
		len += snprintf(buf+len, PAGE_SIZE-len, "[SW ][%d] (%+3d, %+3d, %+3d)\n", 1, 
			obj->cali_sw[BMA255_AXIS_X], obj->cali_sw[BMA255_AXIS_Y], obj->cali_sw[BMA255_AXIS_Z]);

		len += snprintf(buf+len, PAGE_SIZE-len, "[ALL]    (%+3d, %+3d, %+3d) : (%+3d, %+3d, %+3d)\n", 
			obj->offset[BMA255_AXIS_X]*mul + obj->cali_sw[BMA255_AXIS_X],
			obj->offset[BMA255_AXIS_Y]*mul + obj->cali_sw[BMA255_AXIS_Y],
			obj->offset[BMA255_AXIS_Z]*mul + obj->cali_sw[BMA255_AXIS_Z],
			tmp[BMA255_AXIS_X], tmp[BMA255_AXIS_Y], tmp[BMA255_AXIS_Z]);
		
		return len;
    }
}
/*----------------------------------------------------------------------------*/
static ssize_t store_cali_value(struct device_driver *ddri, char *buf, size_t count)
{
	struct i2c_client *client = bma255_i2c_client;  
	int err, x, y, z;
	int dat[BMA255_AXES_NUM];

	if(!strncmp(buf, "rst", 3))
	{
		if(err = BMA255_ResetCalibration(client))
		{
			GSE_ERR("reset offset err = %d\n", err);
		}	
	}
	else if(3 == sscanf(buf, "0x%02X 0x%02X 0x%02X", &x, &y, &z))
	{
		dat[BMA255_AXIS_X] = x;
		dat[BMA255_AXIS_Y] = y;
		dat[BMA255_AXIS_Z] = z;
		if(err = BMA255_WriteCalibration(client, dat))
		{
			GSE_ERR("write calibration err = %d\n", err);
		}		
	}
	else
	{
		GSE_ERR("invalid format\n");
	}
	
	return count;
}


/*----------------------------------------------------------------------------*/
static ssize_t show_firlen_value(struct device_driver *ddri, char *buf)
{
#ifdef CONFIG_BMA255_LOWPASS
	struct i2c_client *client = bma255_i2c_client;
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	if(atomic_read(&obj->firlen))
	{
		int idx, len = atomic_read(&obj->firlen);
		GSE_LOG("len = %2d, idx = %2d\n", obj->fir.num, obj->fir.idx);

		for(idx = 0; idx < len; idx++)
		{
			GSE_LOG("[%5d %5d %5d]\n", obj->fir.raw[idx][BMA255_AXIS_X], obj->fir.raw[idx][BMA255_AXIS_Y], obj->fir.raw[idx][BMA255_AXIS_Z]);
		}
		
		GSE_LOG("sum = [%5d %5d %5d]\n", obj->fir.sum[BMA255_AXIS_X], obj->fir.sum[BMA255_AXIS_Y], obj->fir.sum[BMA255_AXIS_Z]);
		GSE_LOG("avg = [%5d %5d %5d]\n", obj->fir.sum[BMA255_AXIS_X]/len, obj->fir.sum[BMA255_AXIS_Y]/len, obj->fir.sum[BMA255_AXIS_Z]/len);
	}
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&obj->firlen));
#else
	return snprintf(buf, PAGE_SIZE, "not support\n");
#endif
}
/*----------------------------------------------------------------------------*/
static ssize_t store_firlen_value(struct device_driver *ddri, char *buf, size_t count)
{
#ifdef CONFIG_BMA255_LOWPASS
	struct i2c_client *client = bma255_i2c_client;  
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);
	int firlen;

	if(1 != sscanf(buf, "%d", &firlen))
	{
		GSE_ERR("invallid format\n");
	}
	else if(firlen > C_MAX_FIR_LENGTH)
	{
		GSE_ERR("exceeds maximum filter length\n");
	}
	else
	{ 
		atomic_set(&obj->firlen, firlen);
		if(NULL == firlen)
		{
			atomic_set(&obj->fir_en, 0);
		}
		else
		{
			memset(&obj->fir, 0x00, sizeof(obj->fir));
			atomic_set(&obj->fir_en, 1);
		}
	}
#endif    
	return count;
}
/*----------------------------------------------------------------------------*/
static ssize_t show_trace_value(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	struct bma255_i2c_data *obj = obj_i2c_data;
	if (obj == NULL)
	{
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	
	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&obj->trace));     
	return res;    
}
/*----------------------------------------------------------------------------*/
static ssize_t store_trace_value(struct device_driver *ddri, char *buf, size_t count)
{
	struct bma255_i2c_data *obj = obj_i2c_data;
	int trace;
	if (obj == NULL)
	{
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	
	if(1 == sscanf(buf, "0x%x", &trace))
	{
		atomic_set(&obj->trace, trace);
	}	
	else
	{
		GSE_ERR("invalid content: '%s', length = %d\n", buf, count);
	}
	
	return count;    
}
/*----------------------------------------------------------------------------*/
static ssize_t show_status_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;    
	struct bma255_i2c_data *obj = obj_i2c_data;
	if (obj == NULL)
	{
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}	
	
	if(obj->hw)
	{
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: %d %d (%d %d)\n", 
	            obj->hw->i2c_num, obj->hw->direction, obj->hw->power_id, obj->hw->power_vol);   
	}
	else
	{
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");
	}
	return len;    
}
/*----------------------------------------------------------------------------*/
static ssize_t show_power_status_value(struct device_driver *ddri, char *buf)
{
	if(sensor_power)
		printk("G sensor is in work mode, sensor_power = %d\n", sensor_power);
	else
		printk("G sensor is in standby mode, sensor_power = %d\n", sensor_power);

	return 0;
}

/*----------------------------------------------------------------------------*/
static ssize_t show_fifo_mode_value(struct device_driver *ddri, char *buf)
{
	unsigned char data;

	if (bma255_get_fifo_mode(bma255_i2c_client, &data) < 0)
	{
		return sprintf(buf, "Read error\n");
	}
	else
	{
		return sprintf(buf, "%d\n", data);
	}
}

/*----------------------------------------------------------------------------*/
static ssize_t store_fifo_mode_value(struct device_driver *ddri, char *buf, size_t count)
{
	unsigned long data;
	int error;

	if (error = kstrtoul(buf, 10, &data))
	{
		return error;
	}
	if (bma255_set_fifo_mode(bma255_i2c_client, (unsigned char) data) < 0)
	{
		GSE_ERR("invalid content: '%s', length = %d\n", buf, count);
	}

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t show_fifo_framecount_value(struct device_driver *ddri, char *buf)
{
	unsigned char data;

	if (bma255_get_fifo_framecount(bma255_i2c_client, &data) < 0)
	{
		return sprintf(buf, "Read error\n");
	}
	else
	{
		return sprintf(buf, "%d\n", data);
	}
}

/*----------------------------------------------------------------------------*/
static ssize_t store_fifo_framecount_value(struct device_driver *ddri, char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct bma255_i2c_data *obj = obj_i2c_data;

	if (error = kstrtoul(buf, 10, &data))
	{
		return error;
	}
	mutex_lock(&obj->lock);
	obj->fifo_count = (unsigned char)data;
	mutex_unlock(&obj->lock);

	return count;
}


/*----------------------------------------------------------------------------*/
static ssize_t show_fifo_data_out_frame_value(struct device_driver *ddri, char *buf)
{
	int err = 0, i, len = 0;
	unsigned char addr = 0;
	signed char fifo_data_out[MAX_FIFO_F_LEVEL * MAX_FIFO_F_BYTES] = {0};
	/* Select X Y Z axis data output for every fifo frame, not single axis data */
	unsigned char f_len = 6;/* FIXME: ONLY USE 3-AXIS */
	struct bma255_i2c_data *obj = obj_i2c_data;
	s16 acc[BMA255_AXES_NUM];
	s16 databuf[BMA255_AXES_NUM];

	if (obj->fifo_count == 0) {
		return -EINVAL;
	}
/*
	if (bma_i2c_read_block(bma255_i2c_client,
			BMA255_FIFO_DATA_OUTPUT_REG, fifo_data_out,
						obj->fifo_count * f_len) < 0)
*/
	addr = BMA255_FIFO_DATA_OUTPUT_REG;
	if(i2c_dma_read(obj->client,addr,fifo_data_out,obj->fifo_count * f_len)<0)

	{
		GSE_ERR("[a]fatal error\n");
		return sprintf(buf, "Read byte block error\n");
	}

	/* please give attention to the fifo output data format*/
	if (f_len == 6) {
		/* Select X Y Z axis data output for every frame */
		for (i = 0; i < obj->fifo_count; i++) {
			databuf[BMA255_AXIS_X] = ((unsigned char)fifo_data_out[i * f_len + 1] << 8 |
							(unsigned char)fifo_data_out[i * f_len + 0]) >> 4;
			databuf[BMA255_AXIS_Y] = ((unsigned char)fifo_data_out[i * f_len + 3] << 8 |
							(unsigned char)fifo_data_out[i * f_len + 2]) >> 4;
			databuf[BMA255_AXIS_Z] = ((unsigned char)fifo_data_out[i * f_len + 5] << 8 |
							(unsigned char)fifo_data_out[i * f_len + 4]) >> 4;

			/*remap coordinate*/
			acc[obj->cvt.map[BMA255_AXIS_X]] = obj->cvt.sign[BMA255_AXIS_X]*databuf[BMA255_AXIS_X];
			acc[obj->cvt.map[BMA255_AXIS_Y]] = obj->cvt.sign[BMA255_AXIS_Y]*databuf[BMA255_AXIS_Y];
			acc[obj->cvt.map[BMA255_AXIS_Z]] = obj->cvt.sign[BMA255_AXIS_Z]*databuf[BMA255_AXIS_Z];
			len = sprintf(buf, "%d %d %d ", acc[BMA255_AXIS_X], acc[BMA255_AXIS_Y], acc[BMA255_AXIS_Z]);
			buf += len;
			err += len;
		}
	}

	return err;
}

/*----------------------------------------------------------------------------*/
static DRIVER_ATTR(chipinfo,   S_IWUSR | S_IRUGO, show_chipinfo_value,      NULL);
static DRIVER_ATTR(cpsdata, 	 S_IWUSR | S_IRUGO, show_cpsdata_value,    NULL);
static DRIVER_ATTR(cpsopmode,  S_IWUSR | S_IRUGO, show_cpsopmode_value,    store_cpsopmode_value);
static DRIVER_ATTR(cpsrange, 	 S_IWUSR | S_IRUGO, show_cpsrange_value,     store_cpsrange_value);
static DRIVER_ATTR(cpsbandwidth, S_IWUSR | S_IRUGO, show_cpsbandwidth_value,    store_cpsbandwidth_value);
static DRIVER_ATTR(sensordata, S_IWUSR | S_IRUGO, show_sensordata_value,    NULL);
static DRIVER_ATTR(cali,       S_IWUSR | S_IRUGO, show_cali_value,          store_cali_value);
static DRIVER_ATTR(firlen,     S_IWUSR | S_IRUGO, show_firlen_value,        store_firlen_value);
static DRIVER_ATTR(trace,      S_IWUSR | S_IRUGO, show_trace_value,         store_trace_value);
static DRIVER_ATTR(status,               S_IRUGO, show_status_value,        NULL);
static DRIVER_ATTR(powerstatus,               S_IRUGO, show_power_status_value,        NULL);
static DRIVER_ATTR(fifo_mode, S_IWUSR | S_IRUGO, show_fifo_mode_value,    store_fifo_mode_value);
static DRIVER_ATTR(fifo_framecount, S_IWUSR | S_IRUGO, show_fifo_framecount_value,    store_fifo_framecount_value);
static DRIVER_ATTR(fifo_data_frame, S_IRUGO, show_fifo_data_out_frame_value,    NULL);
/*----------------------------------------------------------------------------*/
static struct driver_attribute *bma255_attr_list[] = {
	&driver_attr_chipinfo,     /*chip information*/
	&driver_attr_sensordata,   /*dump sensor data*/
	&driver_attr_cali,         /*show calibration data*/
	&driver_attr_firlen,       /*filter length: 0: disable, others: enable*/
	&driver_attr_trace,        /*trace log*/
	&driver_attr_status,
	&driver_attr_powerstatus,
	&driver_attr_cpsdata,	/*g sensor data for compass tilt compensation*/
	&driver_attr_cpsopmode,	/*g sensor opmode for compass tilt compensation*/
	&driver_attr_cpsrange,	/*g sensor range for compass tilt compensation*/
	&driver_attr_cpsbandwidth,	/*g sensor bandwidth for compass tilt compensation*/
	&driver_attr_fifo_mode,
	&driver_attr_fifo_framecount,
	&driver_attr_fifo_data_frame,
};
/*----------------------------------------------------------------------------*/
static int bma255_create_attr(struct device_driver *driver) 
{
	int idx, err = 0;
	int num = (int)(sizeof(bma255_attr_list)/sizeof(bma255_attr_list[0]));
	if (driver == NULL)
	{
		return -EINVAL;
	}

	for(idx = 0; idx < num; idx++)
	{
		if(err = driver_create_file(driver, bma255_attr_list[idx]))
		{            
			GSE_ERR("driver_create_file (%s) = %d\n", bma255_attr_list[idx]->attr.name, err);
			break;
		}
	}    
	return err;
}
/*----------------------------------------------------------------------------*/
static int bma255_delete_attr(struct device_driver *driver)
{
	int idx ,err = 0;
	int num = (int)(sizeof(bma255_attr_list)/sizeof(bma255_attr_list[0]));

	if(driver == NULL)
	{
		return -EINVAL;
	}

	for(idx = 0; idx < num; idx++)
	{
		driver_remove_file(driver, bma255_attr_list[idx]);
	}
	
	return err;
}

/****************************************************************************** 
 * Function Configuration
******************************************************************************/
static int bma255_open(struct inode *inode, struct file *file)
{
	file->private_data = bma255_i2c_client;

	if(file->private_data == NULL)
	{
		GSE_ERR("null pointer!!\n");
		return -EINVAL;
	}
	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int bma255_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}
/*----------------------------------------------------------------------------*/
static long bma255_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client*)file->private_data;
	struct bma255_i2c_data *obj = (struct bma255_i2c_data*)i2c_get_clientdata(client);	
	char strbuf[BMA255_BUFSIZE];
	void __user *data;
	struct SENSOR_DATA sensor_data;
	long err = 0;
	int cali[3];
//	printk();
	//GSE_FUN(f);
	if(_IOC_DIR(cmd) & _IOC_READ)
	{
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	}
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
	{
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}

	if(err)
	{
		GSE_ERR("access error: %08X, (%2d, %2d)\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch(cmd)
	{
		case GSENSOR_IOCTL_INIT:
			bma255_init_client(client, 0);			
			break;

		case GSENSOR_IOCTL_READ_CHIPINFO:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			
			BMA255_ReadChipInfo(client, strbuf, BMA255_BUFSIZE);
			if(copy_to_user(data, strbuf, strlen(strbuf)+1))
			{
				err = -EFAULT;
				break;
			}				 
			break;	  

		case GSENSOR_IOCTL_READ_SENSORDATA:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			
			BMA255_ReadSensorData(client, strbuf, BMA255_BUFSIZE);
			if(copy_to_user(data, strbuf, strlen(strbuf)+1))
			{
				err = -EFAULT;
				break;	  
			}				 
			break;

		case GSENSOR_IOCTL_READ_GAIN:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}			
			
			if(copy_to_user(data, &gsensor_gain, sizeof(struct GSENSOR_VECTOR3D)))
			{
				err = -EFAULT;
				break;
			}				 
			break;

		case GSENSOR_IOCTL_READ_RAW_DATA:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			BMA255_ReadRawData(client, strbuf);
			if(copy_to_user(data, &strbuf, strlen(strbuf)+1))
			{
				err = -EFAULT;
				break;	  
			}
			break;	  

		case GSENSOR_IOCTL_SET_CALI:
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			if(copy_from_user(&sensor_data, data, sizeof(sensor_data)))
			{
				err = -EFAULT;
				break;	  
			}
			if(atomic_read(&obj->suspend))
			{
				GSE_ERR("Perform calibration in suspend state!!\n");
				err = -EINVAL;
			}
			else
			{
				cali[BMA255_AXIS_X] = sensor_data.x * obj->reso->sensitivity / GRAVITY_EARTH_1000;
				cali[BMA255_AXIS_Y] = sensor_data.y * obj->reso->sensitivity / GRAVITY_EARTH_1000;
				cali[BMA255_AXIS_Z] = sensor_data.z * obj->reso->sensitivity / GRAVITY_EARTH_1000;			  
				err = BMA255_WriteCalibration(client, cali);			 
			}
			break;

		case GSENSOR_IOCTL_CLR_CALI:
			err = BMA255_ResetCalibration(client);
			break;

		case GSENSOR_IOCTL_GET_CALI:
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			if(err = BMA255_ReadCalibration(client, cali))
			{
				break;
			}
			
			sensor_data.x = cali[BMA255_AXIS_X] * GRAVITY_EARTH_1000 / obj->reso->sensitivity;
			sensor_data.y = cali[BMA255_AXIS_Y] * GRAVITY_EARTH_1000 / obj->reso->sensitivity;
			sensor_data.z = cali[BMA255_AXIS_Z] * GRAVITY_EARTH_1000 / obj->reso->sensitivity;
			if(copy_to_user(data, &sensor_data, sizeof(sensor_data)))
			{
				err = -EFAULT;
				break;
			}		
			break;
		

		default:
			GSE_ERR("unknown IOCTL: 0x%08x\n", cmd);
			err = -ENOIOCTLCMD;
			break;
			
	}

	return err;
}


/*----------------------------------------------------------------------------*/
static struct file_operations bma255_fops = {
	//.owner = THIS_MODULE,
	.open = bma255_open,
	.release = bma255_release,
	.unlocked_ioctl = bma255_unlocked_ioctl,
};
/*----------------------------------------------------------------------------*/
static struct miscdevice bma255_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gsensor",
	.fops = &bma255_fops,
};
/*----------------------------------------------------------------------------*/
#ifndef CONFIG_HAS_EARLYSUSPEND
/*----------------------------------------------------------------------------*/
static int bma255_suspend(struct i2c_client *client, pm_message_t msg) 
{
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);    
	int err = 0;
	
	GSE_FUN();    

	if(msg.event == PM_EVENT_SUSPEND)
	{   
		if(obj == NULL)
		{
			GSE_ERR("null pointer!!\n");
			return -EINVAL;
		}
		atomic_set(&obj->suspend, 1);
		if(err = BMA255_SetPowerMode(obj->client, false))
		{
			GSE_ERR("write power control fail!!\n");
			return;
		}       
		BMA255_power(obj->hw, 0);
	}
	return err;
}
/*----------------------------------------------------------------------------*/
static int bma255_resume(struct i2c_client *client)
{
	struct bma255_i2c_data *obj = i2c_get_clientdata(client);        
	int err;
	
	GSE_FUN();

	if(obj == NULL)
	{
		GSE_ERR("null pointer!!\n");
		return -EINVAL;
	}

	BMA255_power(obj->hw, 1);
	if(err = bma255_init_client(client, 0))
	{
		GSE_ERR("initialize client fail!!\n");
		return err;        
	}

	atomic_set(&obj->suspend, 0);

	return 0;
}
/*----------------------------------------------------------------------------*/
#else /*CONFIG_HAS_EARLY_SUSPEND is defined*/
/*----------------------------------------------------------------------------*/
static void bma255_early_suspend(struct early_suspend *h) 
{
	struct bma255_i2c_data *obj = container_of(h, struct bma255_i2c_data, early_drv);   
	int err;
	
	GSE_FUN();    

	if(obj == NULL)
	{
		GSE_ERR("null pointer!!\n");
		return;
	}
	atomic_set(&obj->suspend, 1); 
	if(err = BMA255_SetPowerMode(obj->client, false))
	{
		GSE_ERR("write power control fail!!\n");
		return;
	}

	BMA255_power(obj->hw, 0);
}
/*----------------------------------------------------------------------------*/
static void bma255_late_resume(struct early_suspend *h)
{
	struct bma255_i2c_data *obj = container_of(h, struct bma255_i2c_data, early_drv);         
	int err;
	
	GSE_FUN();

	if(obj == NULL)
	{
		GSE_ERR("null pointer!!\n");
		return;
	}

	BMA255_power(obj->hw, 1);
	if(err = bma255_init_client(obj->client, 0))
	{
		GSE_ERR("initialize client fail!!\n");
		return;        
	}

	atomic_set(&obj->suspend, 0);    
}
/*----------------------------------------------------------------------------*/
#endif /*CONFIG_HAS_EARLYSUSPEND*/

#ifdef USE_NEW_SENSOR_ARCH
// if use  this typ of enable , Gsensor should report inputEvent(x, y, z ,stats, div) to HAL
static int bmc156_acc_open_report_data(int open)
{
	//should queuq work to report event if  is_report_input_direct=true
	return 0;
}

// if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL
static int bmc156_acc_enable_nodata(int en)
{
	int err = 0;

	if(((en == 0) && (sensor_power == false))
			||((en == 1) && (sensor_power == true))) {
		GSE_LOG("Gsensor device have updated!\n");
	} else {
		err = BMA255_SetPowerMode(bma255_i2c_client, !sensor_power);
	}

	return err;
}

static int bmc156_acc_set_delay(u64 ns)
{
	int err = 0;
	int value, sample_delay;

	value = (int)ns/1000/1000;
	if(value <= 5)
	{
		sample_delay = BMA255_BW_200HZ;
	}
	else if(value <= 10)
	{
		sample_delay = BMA255_BW_100HZ;
	}
	else
	{
		sample_delay = BMA255_BW_50HZ;
	}

	if(err != BMA255_SUCCESS ) //0x2C->BW=100Hz
	{
		GSE_ERR("Set delay parameter error!\n");
	}

	if(value >= 50) {
		atomic_set(&obj_i2c_data->filter, 0);
	} else {
#if defined(CONFIG_BMI160_ACC_LOWPASS)
		obj_i2c_data->fir.num = 0;
		obj_i2c_data->fir.idx = 0;
		obj_i2c_data->fir.sum[BMI160_ACC_AXIS_X] = 0;
		obj_i2c_data->fir.sum[BMI160_ACC_AXIS_Y] = 0;
		obj_i2c_data->fir.sum[BMI160_ACC_AXIS_Z] = 0;
		atomic_set(&obj_i2c_data->filter, 1);
#endif
	}

	return 0;
}

static int bmc156_acc_get_data(int* x ,int* y,int* z, int* status)
{
	char buff[BMA255_BUFSIZE];
	/* use acc raw data for gsensor */
	BMA255_ReadSensorData(bma255_i2c_client, buff, BMA255_BUFSIZE);

	sscanf(buff, "%x %x %x", x, y, z);
	*status = SENSOR_STATUS_ACCURACY_MEDIUM;

	return 0;
}
#endif

/*----------------------------------------------------------------------------*/
static int bma255_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_client *new_client;
#ifdef USE_NEW_SENSOR_ARCH
	struct acc_control_path ctl={0};
	struct acc_data_path data={0};
#endif
	struct bma255_i2c_data *obj;
	struct hwmsen_object sobj;
	int err = 0;
	
	GSE_FUN();

	if(!(obj = kzalloc(sizeof(*obj), GFP_KERNEL)))
	{
		err = -ENOMEM;
		goto exit;
	}
	
	memset(obj, 0, sizeof(struct bma255_i2c_data));

	obj->hw = get_cust_acc_hw();
	client->addr=obj->hw->i2c_addr[0];
	printk("bma255_i2c_probe obj->hw =%x\n",obj->hw->i2c_addr[0]);
	if(err = hwmsen_get_convert(obj->hw->direction, &obj->cvt))
	{
		GSE_ERR("invalid direction: %d\n", obj->hw->direction);
		goto exit;
	}

	obj_i2c_data = obj;
	obj->client = client;
	new_client = obj->client;
	i2c_set_clientdata(new_client,obj);
	
	atomic_set(&obj->trace, 0);
	atomic_set(&obj->suspend, 0);
	mutex_init(&obj->lock);
	mutex_init(&i2c_lock);
	
#ifdef CONFIG_BMA255_LOWPASS
	if(obj->hw->firlen > C_MAX_FIR_LENGTH)
	{
		atomic_set(&obj->firlen, C_MAX_FIR_LENGTH);
	}	
	else
	{
		atomic_set(&obj->firlen, obj->hw->firlen);
	}
	
	if(atomic_read(&obj->firlen) > 0)
	{
		atomic_set(&obj->fir_en, 1);
	}
	
#endif

	bma255_i2c_client = new_client;	

	if(err = bma255_init_client(new_client, 1))
	{
		goto exit_init_failed;
	}
#if 0
	//allocate DMA buffer
	I2CDMABuf_va = (u8 *)dma_alloc_coherent(NULL, DMA_BUFFER_SIZE, &I2CDMABuf_pa, GFP_KERNEL);
	if(I2CDMABuf_va == NULL)
	{
		err = -ENOMEM;
		GSE_ERR("Allocate DMA I2C Buffer failed! error = %d\n", err);
		goto exit_dma_alloc_failed;
		
	}
#endif
	if(err = misc_register(&bma255_device))
	{
		GSE_ERR("bma255_device register failed\n");
		goto exit_misc_device_register_failed;
	}
#ifdef USE_NEW_SENSOR_ARCH
    if ((err = bma255_create_attr(&bma255_init_info.platform_diver_addr->driver)))
#else
	if(err = bma255_create_attr(&bma255_gsensor_driver.driver))
#endif
	{
		GSE_ERR("create attribute err = %d\n", err);
		goto exit_create_attr_failed;
	}

#ifdef USE_NEW_SENSOR_ARCH
	ctl.open_report_data= bmc156_acc_open_report_data;
	ctl.enable_nodata = bmc156_acc_enable_nodata;
	ctl.set_delay  = bmc156_acc_set_delay;
	ctl.is_report_input_direct = false;

	err = acc_register_control_path(&ctl);
	if(err)
	{
		GSE_ERR("register acc control path err\n");
		goto exit_kfree;
	}

	data.get_data = bmc156_acc_get_data;
	data.vender_div = 1000;
	err = acc_register_data_path(&data);
	if(err)
	{
		GSE_ERR("register acc data path err\n");
		goto exit_kfree;
	}
#endif
	
#ifdef USE_NEW_SENSOR_ARCH
    bma255_init_flag =1;
#endif
	GSE_LOG("%s: OK\n", __func__);    
	return 0;

	exit_create_attr_failed:
	misc_deregister(&bma255_device);
	exit_misc_device_register_failed:
	exit_dma_alloc_failed:
	exit_init_failed:
	//i2c_detach_client(new_client);
	exit_kfree:
	kfree(obj);
	exit:
	GSE_ERR("%s: err = %d\n", __func__, err);        
	return err;
}

/*----------------------------------------------------------------------------*/
static int bma255_i2c_remove(struct i2c_client *client)
{
	int err = 0;	
#ifdef USE_NEW_SENSOR_ARCH
	if ((err =  bma255_delete_attr(&bma255_init_info.platform_diver_addr->driver)))
#else
	if(err = bma255_delete_attr(&bma255_gsensor_driver.driver))
#endif
	{
		GSE_ERR("bma150_delete_attr fail: %d\n", err);
	}
	
	if(err = misc_deregister(&bma255_device))
	{
		GSE_ERR("misc_deregister fail: %d\n", err);
	}

	if(err = hwmsen_detach(ID_ACCELEROMETER))
	{
		GSE_ERR("hwmsen_detach fail: %d\n", err);
	}	    
#if 0
	//free DMA buffer
	dma_free_coherent(NULL, DMA_BUFFER_SIZE, I2CDMABuf_va, I2CDMABuf_pa);
	I2CDMABuf_va = NULL;
	I2CDMABuf_pa = 0;
#endif
	bma255_i2c_client = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));
	return 0;
}
/*----------------------------------------------------------------------------*/
#ifdef USE_NEW_SENSOR_ARCH
static int gsensor_local_init(void)
{
//	struct acc_hw *hw = get_cust_bma255_acc_hw();
	
	GSE_FUN();

	BMA255_power(hw, 1);
	if(i2c_add_driver(&bma255_i2c_driver))
	{
		GSE_ERR("add driver error\n");
		return -1;
	}
	if(-1 == bma255_init_flag)
	{
	   return -1;
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
static int gsensor_remove(void)
{
//    struct acc_hw *hw = get_cust_bma255_acc_hw();

    GSE_FUN();    
    BMA255_power(hw, 0);    
    i2c_del_driver(&bma255_i2c_driver);
    return 0;
}
#else
static int bma255_probe(struct platform_device *pdev) 
{
//	struct acc_hw *hw = get_cust_bma255_acc_hw();
	
	GSE_FUN();

	BMA255_power(hw, 1);
	if(i2c_add_driver(&bma255_i2c_driver))
	{
		GSE_ERR("add driver error\n");
		return -1;
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
static int bma255_remove(struct platform_device *pdev)
{
//    struct acc_hw *hw = get_cust_bma255_acc_hw();

    GSE_FUN();    
    BMA255_power(hw, 0);    
    i2c_del_driver(&bma255_i2c_driver);
    return 0;
}
/*----------------------------------------------------------------------------*/
static struct platform_driver bma255_gsensor_driver = {
	.probe      = bma255_probe,
	.remove     = bma255_remove,    
	.driver     = {
		.name  = "gsensor",
	}
};
#endif
/*----------------------------------------------------------------------------*/
static int __init bma255_init(void)
{
//	struct acc_hw *hw = get_cust_bma255_acc_hw();
	GSE_FUN();
	hw = get_accel_dts_func(BMA056_NAME, hw);
	if (!hw)
		GSE_ERR("get dts info fail\n");
	GSE_LOG("%s: i2c_number=%d  hw->i2c_add=%xr\n", __func__, hw->i2c_num,hw->i2c_addr[0]);
	acc_driver_add(&bma255_init_info);
	return 0;    
}
/*----------------------------------------------------------------------------*/
static void __exit bma255_exit(void)
{
	GSE_FUN();
#ifndef USE_NEW_SENSOR_ARCH	
	platform_driver_unregister(&bma255_gsensor_driver);
#endif
}
/*----------------------------------------------------------------------------*/
module_init(bma255_init);
module_exit(bma255_exit);
/*----------------------------------------------------------------------------*/
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BMA255 I2C driver");
MODULE_AUTHOR("hongji.zhou@bosch-sensortec.com");

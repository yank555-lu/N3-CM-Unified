/*
 *  Copyright (C) 2010,Imagis Technology Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/unaligned.h>

#include <asm/io.h>
//#include <mach/gpio.h>
//#include <mach/vreg.h>
#include <linux/gpio.h>

#include <linux/regulator/consumer.h>

#include "ist30xx.h"
#include "ist30xx_tracking.h"

/******************************************************************************
 * Return value of Error
 * EPERM  : 1 (Operation not permitted)
 * ENOENT : 2 (No such file or directory)
 * EIO    : 5 (I/O error)
 * ENXIO  : 6 (No such device or address)
 * EINVAL : 22 (Invalid argument)
 *****************************************************************************/

extern struct ist30xx_data *ts_data;
const u32 pos_cmd = cpu_to_be32(CMD_GET_COORD);
struct i2c_msg pos_msg[READ_CMD_MSG_LEN] = {
	{
		.flags = 0,
		.len = IST30XX_ADDR_LEN,
		.buf = (u8 *)&pos_cmd,
	},
	{ .flags = I2C_M_RD, },
};

int ist30xx_get_position(struct i2c_client *client, u32 *buf, u16 len)
{
	int ret, i;

	pos_msg[0].addr = client->addr;
	pos_msg[1].addr = client->addr;
	pos_msg[1].len = len * IST30XX_DATA_LEN,
	pos_msg[1].buf = (u8 *)buf,

	ret = i2c_transfer(client->adapter, pos_msg, READ_CMD_MSG_LEN);
	if (ret != READ_CMD_MSG_LEN) {
		tsp_err("%s: i2c failed (%d)\n", __func__, ret);
		return -EIO;
	}

	for (i = 0; i < len; i++)
		buf[i] = cpu_to_be32(buf[i]);

	return 0;
}

int ist30xx_cmd_run_device(struct i2c_client *client, bool is_reset)
{
	int ret = -EIO;

	if (is_reset == true) ist30xx_reset();
	ret = ist30xx_write_cmd(client, CMD_RUN_DEVICE, 0);

	ist30xx_tracking(TRACK_CMD_RUN_DEVICE);

	msleep(10);

	return ret;
}

int ist30xx_cmd_start_scan(struct i2c_client *client)
{
	int ret;

	ret = ist30xx_write_cmd(client, CMD_START_SCAN, 0);

	ist30xx_tracking(TRACK_CMD_SCAN);

	msleep(100);

	ts_data->status.noise_mode = true;

	return ret;
}

int ist30xx_cmd_calibrate(struct i2c_client *client)
{
	int ret = ist30xx_write_cmd(client, CMD_CALIBRATE, 0);

	ist30xx_tracking(TRACK_CMD_CALIB);

	tsp_info("%s\n", __func__);

	msleep(100);

	return ret;
}

int ist30xx_cmd_check_calib(struct i2c_client *client)
{
	int ret = ist30xx_write_cmd(client, CMD_CHECK_CALIB, 0);

	ist30xx_tracking(TRACK_CMD_CHECK_CALIB);

	tsp_info("*** Check Calibration cmd ***\n");

	msleep(20);

	return ret;
}

int ist30xx_cmd_update(struct i2c_client *client, int cmd)
{
	u32 val = (cmd == CMD_ENTER_FW_UPDATE ? CMD_FW_UPDATE_MAGIC : 0);
	int ret = ist30xx_write_cmd(client, cmd, val);

	ist30xx_tracking(TRACK_CMD_FWUPDATE);

	msleep(10);

	return ret;
}

int ist30xx_cmd_reg(struct i2c_client *client, int cmd)
{
	int ret = ist30xx_write_cmd(client, cmd, 0);

	if (cmd == CMD_ENTER_REG_ACCESS) {
		ist30xx_tracking(TRACK_CMD_ENTER_REG);
		msleep(100);
	} else if (cmd == CMD_EXIT_REG_ACCESS) {
		ist30xx_tracking(TRACK_CMD_EXIT_REG);
		msleep(10);
	}

	return ret;
}


int ist30xx_read_cmd(struct i2c_client *client, u32 cmd, u32 *buf)
{
	int ret;
	u32 le_reg = cpu_to_be32(cmd);

	struct i2c_msg msg[READ_CMD_MSG_LEN] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = IST30XX_ADDR_LEN,
			.buf = (u8 *)&le_reg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = IST30XX_DATA_LEN,
			.buf = (u8 *)buf,
		},
	};

	ret = i2c_transfer(client->adapter, msg, READ_CMD_MSG_LEN);
	if (ret != READ_CMD_MSG_LEN) {
		tsp_err("%s: i2c failed (%d), cmd: %x\n", __func__, ret, cmd);
		return -EIO;
	}
	*buf = cpu_to_be32(*buf);

	return 0;
}


int ist30xx_write_cmd(struct i2c_client *client, u32 cmd, u32 val)
{
	int ret;
	struct i2c_msg msg;
	u8 msg_buf[IST30XX_ADDR_LEN + IST30XX_DATA_LEN];

	put_unaligned_be32(cmd, msg_buf);
	put_unaligned_be32(val, msg_buf + IST30XX_ADDR_LEN);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = IST30XX_ADDR_LEN + IST30XX_DATA_LEN;
	msg.buf = msg_buf;

	ret = i2c_transfer(client->adapter, &msg, WRITE_CMD_MSG_LEN);
	if (ret != WRITE_CMD_MSG_LEN) {
		tsp_err("%s: i2c failed (%d), cmd: %x(%x)\n", __func__, ret, cmd, val);
		return -EIO;
	}

	return 0;
}

int ts_power_enable(int en)
{
	int rc = 0;
	static struct regulator *ldo6;
	tsp_err("%s: %s\n", __func__, (en) ? "on" : "off");

	if(!ldo6){
		ldo6 = regulator_get(NULL, "vdd_l6");
		rc = regulator_set_voltage(ldo6, 1800000, 1800000);
		if (rc){
			printk(KERN_ERR "%s: TSP set_level failed (%d)\n", __func__, rc);
		}
	}

	rc = gpio_direction_output(ts_data->dt_data->touch_en_gpio, en);
	if (rc)
		tsp_err("%s: unable to set_direction for TSP_EN [%d]\n",
				__func__, ts_data->dt_data->touch_en_gpio);

	if(en) {
		if (regulator_is_enabled(ldo6))
			tsp_err("%s: L6(1.8V) is enabled\n", __func__);
		else {
			printk("[TSP] L6 is enabled by TSP now\n");
			rc = regulator_enable(ldo6);
			ts_data->i2cPower_flag = true;
			if (rc)
				tsp_err("%s: TSP enable failed (%d)\n", __func__, rc);
		}
	} else {
		if (ts_data->i2cPower_flag == true && regulator_is_enabled(ldo6)) {
			printk("[TSP] L6 is disabled by TSP now\n");
			rc = regulator_disable(ldo6);
			if (rc)
				tsp_err("%s: TSP disable failed (%d)\n", __func__, rc);
			else
				ts_data->i2cPower_flag = false;
		} else
			tsp_err("%s: L6(1.8V) is disabled\n", __func__);
	}

	tsp_info("%s: touch_en: %d, ldo6: %d\n", __func__,
		gpio_get_value(ts_data->dt_data->touch_en_gpio), regulator_is_enabled(ldo6));
	return rc;
}

#if 0
#define TSP_PWR_LDO_GPIO        41
#define GPIO_TSP_SCL        35
#define GPIO_TSP_SDA        40
static struct regulator *touch_regulator;
static void ts_power_enable(int en)
{
	int ret;
	
	printk(KERN_ERR "%s %s\n", __func__, (en) ? "on" : "off");

	if(touch_regulator == NULL)
	{
		touch_regulator = regulator_get(NULL, "gpldo2_uc"); 
		if(printk(touch_regulator))
			printk("can not get VTOUCH_3.3V\n");
		printk("touch_regulator= %d\n",touch_regulator);
	}
	
	if(en==1)
	{
		ret = regulator_set_voltage(touch_regulator,3000000,3000000); //@Fixed me, HW
		if(ret < 0)
			printk("[TSP] regulator_set_voltage ret = %d \n", ret);   
	
		ret = regulator_enable(touch_regulator);
		if(ret < 0)			
			printk("[TSP] regulator_enable ret = %d \n", ret);       			
	}
	else
	{
		ret = regulator_disable(touch_regulator);
		if(ret < 0)		
			printk("regulator_disable ret = %d \n", ret);			
	}
}
#endif

int ist30xx_power_on(void)
{
	int rc = 0;
	if (ts_data->status.power != 1) {
		tsp_info("%s()\n", __func__);
		/* VDD enable */
		msleep(5);
		/* VDDIO enable */
		ist30xx_tracking(TRACK_PWR_ON);
		rc = ts_power_enable(1);
		msleep(100);
		
		if(!rc) /*power is enabled successfully*/
			ts_data->status.power = 1;
	}

	return rc;
}


int ist30xx_power_off(void)
{
	int rc = 0;
	if (ts_data->status.power != 0) {
		tsp_info("%s()\n", __func__);
		/* VDDIO disable */
		msleep(5);

		/* VDD disable */
		ist30xx_tracking(TRACK_PWR_OFF);
		rc = ts_power_enable(0);

		msleep(50);

		if(!rc) /*power is disabled successfully*/
			ts_data->status.power = 0;

		ts_data->status.noise_mode = false;
	}

	return rc;
}


int ist30xx_reset(void)
{
	tsp_info("%s()\n", __func__);
	ist30xx_power_off();
	msleep(10);
	ist30xx_power_on();

	ts_data->status.power = 1;
	return 0;
}


int ist30xx_internal_suspend(struct ist30xx_data *data)
{
	ist30xx_power_off();
	return 0;
}


int ist30xx_internal_resume(struct ist30xx_data *data)
{
	ist30xx_power_on();
	ist30xx_cmd_run_device(data->client, false);
	return 0;
}


int ist30xx_init_system(void)
{
	int ret;

	// TODO : place additional code here.
#if 1
	ret = ist30xx_power_on();
	if (ret) {
		tsp_err("%s: ist30xx_power_on failed (%d)\n", __func__, ret);
		return -EIO;
	}
#endif

#if 0
	ret = ist30xx_reset();
	if (ret) {
		tsp_err("%s: ist30xx_reset failed (%d)\n", __func__, ret);
		return -EIO;
	}
#endif

	return 0;
}

/*
 * driver/misc/sm5703-muic.c - SM5703 MUIC micro USB switch device driver
 *
 * Copyright (C) 2013 Samsung Electronics
 * Jeongrae Kim <jryu.kim@samsung.com>
 * Nitin Chaudhary <nc.chaudhary@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/i2c/sm5703-muic.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/pmic8058.h>
#include <linux/input.h>
#include <linux/switch.h>
#if defined(CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#endif

#ifdef CONFIG_USB_NOTIFY_LAYER
#include <linux/usb_notify.h>
#endif

#if defined(CONFIG_MUIC_NOTI)
#include <linux/i2c/muic_notifier.h>
#endif

/* spmi control */
extern int spmi_ext_register_writel_extra(u8 sid, u16 ad, u8 *buf, int len);
extern int spmi_ext_register_readl_extra(u8 sid, u16 ad, u8 *buf, int len);

//extern int system_rev;

#define INT_MASK1                   0x5C
#define INT_MASK2                   0x00

/* DEVICE ID */
#define SM5703_MUIC_DEV_ID          0x01  /* Vendor ID */
#define SM5703_MUIC_DEV_ID_REV      0x00  /* Rev ID */

/* SM5703 MUIC I2C registers */
#define REG_DEVICE_ID               0x01
#define REG_CONTROL                 0x02
#define REG_INT1                    0x03
#define REG_INT2                    0x04
#define REG_INT_MASK1               0x05
#define REG_INT_MASK2               0x06
#define REG_ADC                     0x07
#define REG_TIMING_SET1             0x08
#define REG_TIMING_SET2             0x09
#define REG_DEVICE_TYPE1            0x0A
#define REG_DEVICE_TYPE2            0x0B
#define REG_BUTTON1                 0x0C
#define REG_BUTTON2                 0x0D
#define REG_CARKIT_STATUS           0x0E
#define REG_MANUAL_SW1              0x13
#define REG_MANUAL_SW2              0x14
#define REG_DEVICE_TYPE3            0x15
#define REG_RESET                   0x1B
#define REG_VBUSINVALID             0x1D
#define REG_TIMER_SET               0x20
#define REG_BCD_RESCAN              0x21
#define REG_CHG_TYPE                0x24
#define REG_CHGPUMP_SET             0x3A


#define DATA_NONE                   0x00

/* Control */
#define CON_SWITCH_OPEN     (1 << 4)
#define CON_RAW_DATA        (1 << 3)
#define CON_MANUAL_SW       (1 << 2)
#define CON_WAIT            (1 << 1)
#define CON_INT_MASK        (1 << 0)
#define CON_MASK        (CON_SWITCH_OPEN | CON_RAW_DATA | \
		CON_MANUAL_SW | CON_WAIT)

/* Device Type 1 */
#define DEV_USB_OTG         (1 << 7)
#define DEV_DEDICATED_CHG   (1 << 6)
#define DEV_USB_CHG         (1 << 5)
#define DEV_CAR_KIT         (1 << 4)
#define DEV_UART            (1 << 3)
#define DEV_USB             (1 << 2)
#define DEV_AUDIO_2         (1 << 1)
#define DEV_AUDIO_1         (1 << 0)

#define DEV_T1_USB_MASK            (DEV_USB_OTG | DEV_USB_CHG | DEV_USB)
#define DEV_T1_CHARGER_MASK        (DEV_DEDICATED_CHG | DEV_CAR_KIT)
#define DEV_CARKIT_CHARGER1_MASK   (1 << 1)
#define MANSW1_OPEN_RUSTPROOF      ((0x0 << 5) | (0x3 << 2) | (1 << 0))

/* Device Type 2 */

#define DEV_LANHUB         (1 << 9)

#define DEV_AUDIO_DOCK     (1 << 8)
#define DEV_SMARTDOCK      (1 << 7)
#define DEV_AV             (1 << 6)
#define DEV_TTY            (1 << 5)
#define DEV_PPD            (1 << 4)
#define DEV_JIG_UART_OFF   (1 << 3)
#define DEV_JIG_UART_ON    (1 << 2)
#define DEV_JIG_USB_OFF    (1 << 1)
#define DEV_JIG_USB_ON     (1 << 0)

#define DEV_T2_USB_MASK     (DEV_JIG_USB_OFF | DEV_JIG_USB_ON)
#define DEV_T2_UART_MASK    (DEV_JIG_UART_OFF)
#define DEV_T2_JIG_MASK     (DEV_JIG_USB_OFF | DEV_JIG_USB_ON | \
		DEV_JIG_UART_OFF)
#define DEV_T2_JIG_ALL_MASK (DEV_JIG_USB_OFF | DEV_JIG_USB_ON | \
		DEV_JIG_UART_OFF | DEV_JIG_UART_ON)

/* Device Type 3 */
#define DEV_MHL             (1 << 0)
#define DEV_NON_STANDARD    (1 << 2)
#define DEV_AV_VBUS         (1 << 4)
#define DEV_U200_CHARGER    (1 << 6)

#define DEV_T3_CHARGER_MASK DEV_U200_CHARGER

/* vbusin valid */
#define DEV_VBUSIN_VALID    (1 << 1)
/* vbusout valid */
#define DEV_VBUSOUT_VALID   (1 << 0)


/*
 * Manual Switch
 * D- [7:5] / D+ [4:2]
 * 000: Open all / 001: USB / 010: AUDIO / 011: UART / 100: V_AUDIO
 */
#define SW_VAUDIO       ((4 << 5) | (4 << 2) | (1 << 1) | (1 << 0))
#define SW_UART         ((3 << 5) | (3 << 2))
#define SW_AUDIO        ((2 << 5) | (2 << 2) | (1 << 0))
#define SW_DHOST        ((1 << 5) | (1 << 2) | (1 << 0))
#define SW_AUTO         ((0 << 5) | (0 << 2))
#define SW_USB_OPEN     (1 << 0)
#define SW_ALL_OPEN     (0)
#define SW_ALL_OPEN_WITH_VBUS      ((0 << 5) | (0 << 2) | (1 << 0))
#define SW_ALL_OPEN_WITHOUT_VBUS   0x01

/* Interrupt 1 */
#define INT_OVP_DISABLE         (1 << 7)
#define INT_OVP_ENABLE          (1 << 5)
#define INT_LONG_KEY_RELEASE    (1 << 4)
#define INT_LONG_KEY_PRESS      (1 << 3)
#define INT_KEY_PRESS           (1 << 2)
#define INT_DETACH              (1 << 1)
#define INT_ATTACH              (1 << 0)

/* Interrupt 2 */
#define INT_VBUSDET_ON          (1 << 7)
#define INT_RID_CHARGER         (1 << 6)
#define INT_MHL                 (1 << 5)
#define INT_STUCK_KEY_RCV       (1 << 4)
#define INT_STUCK_KEY           (1 << 3)
#define INT_ADC_CHANGE          (1 << 2)
#define INT_RESERVED_ATTACH     (1 << 1)
#define INT_VBUS_OFF            (1 << 0)

/* ADC VALUE */
#define ADC_OTG               0x00
#define ADC_MHL               0x20  /* MHL  : bit5 set */
#define ADC_VZW_DOCK          0x0E
#define ADC_VZW_INCOMPATIBLE  0x0F
#define ADC_SMART_DOCK        0x10
#define ADC_HMT               0x11
#define ADC_AUDIO_DOCK        0x12
#define ADC_LANHUB            0x13
#define ADC_CHARGING_CABLE    0x14
#define ADC_MPOS              0x15
#define ADC_UART              0x16
#define ADC_LCABLE            0x17
#define ADC_JIG_USB_OFF       0x18
#define ADC_JIG_USB_ON        0x19
#define ADC_DESKDOCK          0x1a
#define ADC_CEA2              0x1b
#define ADC_JIG_UART_OFF      0x1c
#define ADC_JIG_UART_ON       0x1d
#define ADC_CARDOCK           0x1d
#define ADC_OPEN              0x1f

int uart_sm5703_muic_connecting;
EXPORT_SYMBOL(uart_sm5703_muic_connecting);
int detached_sm5703_muic_status;
EXPORT_SYMBOL(detached_sm5703_muic_status);
static int jig_state;

static int BCD_scan; /* L Company Cable Detection */


struct sm5703_muic_usbsw {
	struct i2c_client       *client;
	struct sm5703_muic_platform_data *pdata;
	int             dev1;
	int             dev2;
	int             dev3;
	int             mansw;
	int		mode;
	int             vbus;
	int             dock_attached;
	int             dev_id;
	int             carkit_dev;
	struct delayed_work     init_work;
	struct mutex            mutex;
	int             adc;
	bool                undefined_attached;
	/* muic current attached device */
	muic_attached_dev		attached_dev;
#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
	unsigned int            previous_dock;
	unsigned int            lanhub_ta_status;
#endif
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
	bool                is_rustproof;
#endif
};

static struct sm5703_muic_usbsw *local_usbsw;

#if defined(CONFIG_SEC_O7_PROJECT) || defined(CONFIG_SEC_XCOVER3_PROJECT) ||defined(CONFIG_SEC_J7_PROJECT)
static struct regulator *fullup;
#endif
static int sm5703_muic_attach_dev(struct sm5703_muic_usbsw *usbsw);
static int sm5703_muic_detach_dev(struct sm5703_muic_usbsw *usbsw);

static int sm5703_muic_write_reg(struct i2c_client *client, int reg, int val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0)
		dev_err(&client->dev, "%s, i2c write error %d\n",
				__func__, ret);
	return ret;
}

static int sm5703_muic_read_reg(struct i2c_client *client, int reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "%s, i2c read error %d\n",
				__func__, ret);
	return ret;
}

#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)

bool lanhub_ta_case = false;

/* RAW DATA Detection*/
static void sm5703_muic_enable_rawdataInterrupts(struct sm5703_muic_usbsw *usbsw)
{
	struct i2c_client *client = usbsw->client;
	u8 value, ret;

	value = sm5703_muic_read_reg(client, REG_CONTROL);
	value &= 0xF7;  /*Control Register Bit 4 set to 0 to enable RAW Data INTR*/

	ret = sm5703_muic_write_reg(client, REG_CONTROL, value);
	pr_info("%s:set CONTROL value to 0x%x\n", __func__, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	/* manual sw2 */
	value = sm5703_muic_read_reg(client, REG_MANUAL_SW2);
	value |= 0x02;  /* manual sw2 Register Bit 1 set to 0 */

	ret = sm5703_muic_write_reg(client, REG_MANUAL_SW2, value);
	pr_info("%s:set Manual SW2 to 0x%x\n", __func__, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

}

static void sm5703_muic_disable_rawdataInterrupts(struct sm5703_muic_usbsw *usbsw)
{
	struct i2c_client *client = local_usbsw->client;
	int value, ret;

	value = sm5703_muic_read_reg(client, REG_CONTROL);
	value |= 0x08;  /*Control Register Bit 4 set to 0 to enable RAW Data INTR*/

	ret = sm5703_muic_write_reg(client, REG_CONTROL, value);
	pr_info("%s:set CONTROL value to 0x%x\n", __func__, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	/* manual sw2 */
	value = sm5703_muic_read_reg(client, REG_MANUAL_SW2);
	value &= 0xFD;  /*manual sw2 Register Bit 1 set to 1*/

	ret = sm5703_muic_write_reg(client, REG_MANUAL_SW2, value);
	pr_info("%s:set Manual SW2 to 0x%x\n", __func__, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
}
#endif

#if defined(CONFIG_MUIC_SUPPORT_DESKDOCK) || defined(CONFIG_USB_HOST_NOTIFY) ||\
	defined(CONFIG_MUIC_SUPPORT_SMARTDOCK)
static void sm5703_muic_dock_control(struct sm5703_muic_usbsw *usbsw,
		int dock_type, int state, int path)
{
	struct i2c_client *client = usbsw->client;
	struct sm5703_muic_platform_data *pdata = usbsw->pdata;
	int ret;

	if (state) {
		usbsw->mansw = path;
		pdata->callback(dock_type, state);
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1, path);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_read_reg(client, REG_CONTROL);
		if (ret < 0) {
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
		} else {
			ret = sm5703_muic_write_reg(client,
					REG_CONTROL, ret & ~CON_MANUAL_SW);
		}
		if (ret < 0)
			dev_err(&client->dev, "%s: err %x\n", __func__, ret);
	} else {
		pdata->callback(dock_type, state);
		ret = sm5703_muic_read_reg(client, REG_CONTROL);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_write_reg(client, REG_CONTROL,
				ret | CON_MANUAL_SW | CON_RAW_DATA);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	}
}
#endif
static void sm5703_muic_reg_init(struct sm5703_muic_usbsw *usbsw)
{
	struct i2c_client *client = usbsw->client;
	unsigned int ctrl = CON_MASK;
	int ret;

	pr_info("sm5703_muic_reg_init is called\n");

	usbsw->dev_id = sm5703_muic_read_reg(client, REG_DEVICE_ID);
	local_usbsw->dev_id = usbsw->dev_id;
	if (usbsw->dev_id < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, usbsw->dev_id);

	dev_info(&client->dev, " sm5703_muic_reg_init dev ID: 0x%x\n",
			usbsw->dev_id);

	ret = sm5703_muic_write_reg(client, REG_INT_MASK1, INT_MASK1);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	ret = sm5703_muic_write_reg(client, REG_INT_MASK2, INT_MASK2);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	/* CON_MASK = (CON_SWITCH_OPEN | CON_RAW_DATA | CON_MANUAL_SW | CON_WAIT) */
	ret = sm5703_muic_write_reg(client, REG_CONTROL, ctrl);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	usbsw->mode = CON_MANUAL_SW;
	/* set timing1 to 300ms */
	ret = sm5703_muic_write_reg(client, REG_TIMING_SET1, 0x04);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	/* Modify from supplier, to enble charge pump to enable the negative power supply. */
	/* IF not, to insert earphone and play mp3 with the maximum volume, messy code output UART. */
	ret = sm5703_muic_write_reg(client, REG_CHGPUMP_SET, 0x00);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
}

static ssize_t sm5703_muic_show_attached_dev(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);

	pr_info("[MUIC] SM5703_MUIC:%s attached_dev:%d\n",
					__func__, usbsw->attached_dev);

	switch (usbsw->attached_dev) {
	case ATTACHED_DEV_NONE_MUIC:
		return sprintf(buf, "No VPS\n");
	case ATTACHED_DEV_USB_MUIC:
		return sprintf(buf, "USB\n");
	case ATTACHED_DEV_CDP_MUIC:
		return sprintf(buf, "CDP\n");
	case ATTACHED_DEV_OTG_MUIC:
		return sprintf(buf, "OTG\n");
	case ATTACHED_DEV_TA_MUIC:
		return sprintf(buf, "TA\n");
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		return sprintf(buf, "JIG UART OFF\n");
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
		return sprintf(buf, "JIG UART OFF/VB\n");
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
		return sprintf(buf, "JIG UART ON\n");
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
		return sprintf(buf, "JIG USB OFF\n");
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		return sprintf(buf, "JIG USB ON\n");
	case ATTACHED_DEV_DESKDOCK_MUIC:
		return sprintf(buf, "DESKDOCK\n");
	case ATTACHED_DEV_SMARTDOCK_MUIC:
		return sprintf(buf, "SMARTDOCK\n");
	case ATTACHED_DEV_AUDIODOCK_MUIC:
		return sprintf(buf, "AUDIODOCK\n");
	default:
		break;
	}

	return sprintf(buf, "UNKNOWN\n");
}

static ssize_t sm5703_muic_show_control(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int value;

	value = i2c_smbus_read_byte_data(client, REG_CONTROL);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	return snprintf(buf, 13, "CONTROL: %02x\n", value);
}

static ssize_t sm5703_muic_show_device_type(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int value;

	value = sm5703_muic_read_reg(client, REG_DEVICE_TYPE1);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	return snprintf(buf, 11, "DEV_TYP %02x\n", value);
}

static ssize_t sm5703_muic_show_manualsw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int value;

	value = sm5703_muic_read_reg(client, REG_MANUAL_SW1);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	if (value == SW_VAUDIO)
		return snprintf(buf, 7, "VAUDIO\n");
	else if (value == SW_UART)
		return snprintf(buf, 5, "UART\n");
	else if (value == SW_AUDIO)
		return snprintf(buf, 6, "AUDIO\n");
	else if (value == SW_DHOST)
		return snprintf(buf, 6, "DHOST\n");
	else if (value == SW_AUTO)
		return snprintf(buf, 5, "AUTO\n");
	else
		return snprintf(buf, 4, "%x", value);
}

static ssize_t sm5703_muic_set_manualsw(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int value, ret;
	unsigned int path = 0;

	value = sm5703_muic_read_reg(client, REG_CONTROL);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	if ((value & ~CON_MANUAL_SW) !=
			(CON_SWITCH_OPEN | CON_RAW_DATA | CON_WAIT))
		return 0;

	if (!strncmp(buf, "VAUDIO", 6)) {
		path = SW_VAUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "UART", 4)) {
		path = SW_UART;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "AUDIO", 5)) {
		path = SW_AUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "DHOST", 5)) {
		path = SW_DHOST;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "AUTO", 4)) {
		path = SW_AUTO;
		value |= CON_MANUAL_SW;
	} else {
		dev_err(dev, "Wrong command\n");
		return 0;
	}

	usbsw->mansw = path;

	ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1, path);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	ret = sm5703_muic_write_reg(client, REG_CONTROL, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return count;
}
static ssize_t sm5703_muic_show_usb_state(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int device_type1, device_type2;

	device_type1 = i2c_smbus_read_byte_data(client, REG_DEVICE_TYPE1);
	if (device_type1 < 0) {
		dev_err(&client->dev, "%s: err %d ", __func__, device_type1);
		return (ssize_t)device_type1;
	}
	device_type2 = i2c_smbus_read_byte_data(client, REG_DEVICE_TYPE2);
	if (device_type2 < 0) {
		dev_err(&client->dev, "%s: err %d ", __func__, device_type2);
		return (ssize_t)device_type2;
	}

	if (device_type1 & DEV_T1_USB_MASK || device_type2 & DEV_T2_USB_MASK)
		return snprintf(buf, 22, "USB_STATE_CONFIGURED\n");

	return snprintf(buf, 25, "USB_STATE_NOTCONFIGURED\n");
}

static ssize_t sm5703_muic_show_adc(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int adc;

	adc = sm5703_muic_read_reg(client, REG_ADC);
	if (adc < 0) {
		dev_err(&client->dev,
				"%s: err at read adc %d\n", __func__, adc);
		return snprintf(buf, 9, "UNKNOWN\n");
	}

	return snprintf(buf, 4, "%x\n", adc);
}

static ssize_t sm5703_muic_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	if (!strncmp(buf, "1", 1)) {
		dev_info(&client->dev,
				"sm5703 muic reset after delay 1000 msec.\n");
		msleep(1000);
		sm5703_muic_write_reg(client, REG_RESET, 0x01);

		dev_info(&client->dev, "sm5703_muic_reset_control done!\n");
	} else {
		dev_info(&client->dev,
				"sm5703_muic_reset_control, but not reset_value!\n");
	}

#ifdef CONFIG_MUIC_SUPPORT_RUSTPROOF
	usbsw->is_rustproof = false;
#endif
	usbsw->attached_dev = ATTACHED_DEV_NONE_MUIC;

	sm5703_muic_reg_init(usbsw);

	return count;
}

#ifdef CONFIG_MUIC_SUPPORT_RUSTPROOF
static void muic_rustproof_feature(struct i2c_client *client, int state);
/* Keystring "*#0*#" sysfs implementation */
static ssize_t uart_en_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	/* is_rustproof is false then UART can be enabled */
	return snprintf(buf, 4, "%d\n", !(usbsw->is_rustproof));
}

static ssize_t uart_en_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	if (!strncmp(buf, "1", 1)) {
		dev_info(&client->dev,
				"[MUIC]Runtime enabling the UART.\n");
		usbsw->is_rustproof = false;
		muic_rustproof_feature(client, SM5703_MUIC_DETACHED);

	} else {
		dev_info(&client->dev,
				"[MUIC]Runtime disabling the UART.\n");
		usbsw->is_rustproof = true;
	}
	/* reinvoke the attach detection function to set proper paths */
	sm5703_muic_attach_dev(usbsw);

	return size;
}

static DEVICE_ATTR(uart_en, S_IRUGO | S_IWUSR,
		uart_en_show, uart_en_store);

static ssize_t uart_sel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* for sm5703 muic paths are always switch to AP */
	return snprintf(buf, 4, "AP\n");
}

static ssize_t uart_sel_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	dev_info(&client->dev, "[MUIC]Enabling AP UART Path, dummy Call\n");
	return size;
}

static DEVICE_ATTR(uart_sel, S_IRUGO | S_IWUSR,
		uart_sel_show, uart_sel_store);

static ssize_t usbsel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 5, "PDA\n");
}

static ssize_t usbsel_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct sm5703_muic_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	dev_info(&client->dev, "[MUIC]Enabling AP UART Path, dummy Call\n");
	return size;
}

static DEVICE_ATTR(usb_sel, S_IRUGO | S_IWUSR,
		usbsel_show, usbsel_store);
#endif

static DEVICE_ATTR(control, S_IRUGO, sm5703_muic_show_control, NULL);
static DEVICE_ATTR(device_type, S_IRUGO, sm5703_muic_show_device_type, NULL);
static DEVICE_ATTR(switch, S_IRUGO | S_IWUSR,
		sm5703_muic_show_manualsw, sm5703_muic_set_manualsw);
static DEVICE_ATTR(usb_state, S_IRUGO, sm5703_muic_show_usb_state, NULL);
static DEVICE_ATTR(adc, S_IRUGO, sm5703_muic_show_adc, NULL);
static DEVICE_ATTR(reset_switch, S_IWUSR | S_IWGRP, NULL, sm5703_muic_reset);
static DEVICE_ATTR(attached_dev, S_IRUGO, sm5703_muic_show_attached_dev, NULL);

static struct attribute *sm5703_muic_attributes[] = {
	&dev_attr_control.attr,
	&dev_attr_device_type.attr,
	&dev_attr_switch.attr,
	NULL
};

static const struct attribute_group sm5703_muic_group = {
	.attrs = sm5703_muic_attributes,
};


#if defined(CONFIG_USB_HOST_NOTIFY)
static void sm5703_muic_set_otg(struct sm5703_muic_usbsw *usbsw, int state)
{
	int ret;
	struct i2c_client *client = usbsw->client;

	if (state == SM5703_MUIC_ATTACHED) {
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1, 0x25);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		/* Disconnecting the MUIC_ID & ITBP Pins */
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW2, 0x02);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_read_reg(client, REG_CONTROL);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = ret & 0xFB; /* Manual Connection S/W enable */
		ret = sm5703_muic_write_reg(client, REG_CONTROL, ret);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
	} else {
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW2, 0x00);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1,
				SW_ALL_OPEN);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_read_reg(client, REG_CONTROL);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = ret | 0x04; /* Manual Connection S/W Disable */
		ret = sm5703_muic_write_reg(client, REG_CONTROL, ret);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
	}
}
#endif

#if defined(CONFIG_VIDEO_MHL_V2)
int dock_det(void)
{
	return local_usbsw->dock_attached;
}
EXPORT_SYMBOL(dock_det);
#endif

int check_sm5703_muic_jig_state(void)
{
	return jig_state;
}
EXPORT_SYMBOL(check_sm5703_muic_jig_state);

#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
static void sm5703_muic_set_lanhub(struct sm5703_muic_usbsw *usbsw, int state)
{
	int ret;
	struct i2c_client *client = usbsw->client;

	if (state == SM5703_MUIC_ATTACHED) {
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1, 0x25);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		/* Disconnecting the MUIC_ID & ITBP Pins */
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW2, 0x02);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_read_reg(client, REG_CONTROL);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = ret & 0xFB; /* Manual Connection S/W enable */
		ret = sm5703_muic_write_reg(client, REG_CONTROL, ret);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
	} else {
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW2, 0x00);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1,
				SW_ALL_OPEN);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = sm5703_muic_read_reg(client, REG_CONTROL);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
		ret = ret | 0x04; /* Manual Connection S/W enable */
		ret = sm5703_muic_write_reg(client, REG_CONTROL, ret);
		if (ret < 0)
			dev_info(&client->dev, "%s: err %d\n", __func__, ret);
	}
}

static void sm5703_muic_mask_vbus_detect(struct sm5703_muic_usbsw *usbsw, int state)
{
	unsigned int value;
	struct i2c_client *client = usbsw->client;

	if (state == SM5703_MUIC_ATTACHED) {
		pr_info("%s called, state: (%d)\n", __func__, state);
		/* Need to disable the vbus change interrupts */
		value = sm5703_muic_read_reg(client, REG_INT_MASK2);
		if (value < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, value);
		value |= 0x01;
		value = sm5703_muic_write_reg(client, REG_INT_MASK2, value);
		if (value < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, value);
	} else {
		pr_info("%s called, state: (%d)\n", __func__, state);

		value = sm5703_muic_write_reg(client, REG_INT_MASK2, INT_MASK2);
		if (value < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, value);
	}

}

static int sm5703_muic_detect_lanhub(struct sm5703_muic_usbsw *usbsw)
{
	unsigned int dev1, dev2, adc;
	struct sm5703_muic_platform_data *pdata = usbsw->pdata;
	struct i2c_client *client = usbsw->client;
	pr_info("%s called\n", __func__);

	dev1 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE1);
	if (dev1 < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, dev1);
		return dev1;
	}

	dev2 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE2);
	if (dev2 < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, dev2);
		return dev2;
	}

	adc = sm5703_muic_read_reg(client, REG_ADC);


	dev_info(&client->dev, "dev1: 0x%02x, dev2: 0x%02x, adc: 0x%02x\n",
			dev1, dev2, adc);

	/* Attached + Detached */
	switch (adc) {
	case ADC_OTG:
		lanhub_ta_case = false;
		usbsw->adc = adc;
		sm5703_muic_set_otg(usbsw, SM5703_MUIC_ATTACHED);
		if (usbsw->previous_dock == SM5703_MUIC_NONE) {
			dev_info(&client->dev, "%s:LANHUB Connected\n", __func__);
			pdata->callback(CABLE_TYPE_OTG, SM5703_MUIC_ATTACHED);
			sm5703_muic_set_otg(usbsw, SM5703_MUIC_ATTACHED);
		} else if (usbsw->previous_dock == ADC_LANHUB) {
			dev_info(&client->dev, "%s:Switch LANHUB+TA to LANHUB\n", __func__);
			usbsw->lanhub_ta_status = 0;
			pdata->lanhub_cb(CABLE_TYPE_LANHUB, SM5703_MUIC_DETACHED, LANHUB_TA);
		}
		sm5703_muic_mask_vbus_detect(usbsw, SM5703_MUIC_DETACHED);
		usbsw->dock_attached = SM5703_MUIC_ATTACHED;
		usbsw->previous_dock = ADC_OTG;
		break;
	case ADC_LANHUB:
		usbsw->adc = adc;
		lanhub_ta_case = true;
		usbsw->lanhub_ta_status = 1;
		sm5703_muic_mask_vbus_detect(usbsw, SM5703_MUIC_ATTACHED);
		usbsw->dock_attached = SM5703_MUIC_ATTACHED;
		usbsw->mansw = SW_DHOST;
		if (usbsw->previous_dock == SM5703_MUIC_NONE) {
			dev_info(&client->dev, "%s:LANHUB+TA Connected\n", __func__);
			pdata->lanhub_cb(CABLE_TYPE_LANHUB, SM5703_MUIC_ATTACHED, LANHUB);
			sm5703_muic_set_lanhub(usbsw, SM5703_MUIC_ATTACHED);
		} else if (usbsw->previous_dock == ADC_OTG) {
			dev_info(&client->dev, "%s:Switch LANHUB to LANHUB+TA\n", __func__);
			pdata->lanhub_cb(CABLE_TYPE_LANHUB, SM5703_MUIC_ATTACHED, LANHUB_TA);
		}
		usbsw->previous_dock = ADC_LANHUB;
		break;
	case ADC_OPEN:
		usbsw->adc = adc;
		dev_info(&client->dev, "%s:LANHUB + TA -> ADC_OPEN case\n", __func__);
		if (pdata->lanhub_cb && usbsw->lanhub_ta_status == 1) {
			if (usbsw->previous_dock == ADC_LANHUB)
				pdata->lanhub_cb(CABLE_TYPE_LANHUB, SM5703_MUIC_DETACHED, LANHUB);
			sm5703_muic_disable_rawdataInterrupts(usbsw);
			usbsw->lanhub_ta_status = 0;
			sm5703_muic_mask_vbus_detect(usbsw, SM5703_MUIC_DETACHED);
			pdata->callback(CABLE_TYPE_OTG, SM5703_MUIC_DETACHED);
			sm5703_muic_set_otg(usbsw, SM5703_MUIC_DETACHED);
		} else if (usbsw->previous_dock == ADC_OTG) {
			sm5703_muic_disable_rawdataInterrupts(usbsw);
			pdata->callback(CABLE_TYPE_OTG, SM5703_MUIC_DETACHED);
			sm5703_muic_set_otg(usbsw, SM5703_MUIC_DETACHED);
		} else {
			dev_info(&client->dev, "%s:ignore ADC_OPEN case\n", __func__);
			sm5703_muic_mask_vbus_detect(usbsw, SM5703_MUIC_DETACHED);
		}

		usbsw->previous_dock = SM5703_MUIC_NONE;
		lanhub_ta_case = false;
		break;
	default:
		dev_info(&client->dev, "%s:Not reaching here(adc:0x%02x)\n",
				__func__, adc);
		lanhub_ta_case = false;
		break;
	}

	usbsw->dev1 = dev1;
	usbsw->dev2 = dev2;
	return adc;
}
#endif
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
static void muic_rustproof_feature(struct i2c_client *client, int state)
{
	int val;
	if (state) {
		val = sm5703_muic_write_reg(client, REG_MANUAL_SW1,
				SW_ALL_OPEN_WITH_VBUS);
		if (val < 0)
			dev_info(&client->dev, "%s:MANUAL SW1,err %d\n", __func__, val);
		val = sm5703_muic_write_reg(client, REG_MANUAL_SW2, 0x04);
		if (val < 0)
			dev_info(&client->dev, "%s: MANUAL SW2,err %d\n", __func__, val);
		val = sm5703_muic_read_reg(client, REG_CONTROL);
		if (val < 0)
			dev_info(&client->dev, "%s:CTRL REG,err %d\n", __func__, val);
		val &= 0xFB;
		val = sm5703_muic_write_reg(client, REG_CONTROL, val);
		if (val < 0)
			dev_info(&client->dev, "%s:CTRL REG,err %d\n", __func__, val);
	} else {
		val = sm5703_muic_read_reg(client, REG_CONTROL);
		if (val < 0)
			dev_info(&client->dev, "%s: CTRL REG,err %d\n", __func__, val);
		val = val | 0x04; /* Automatic Connection S/W enable */
		val = sm5703_muic_write_reg(client, REG_CONTROL, val);
		if (val < 0)
			dev_info(&client->dev, "%s: CTRL REG,err %d\n", __func__, val);
		val = sm5703_muic_write_reg(client, REG_MANUAL_SW2, 0x00);
		if (val < 0)
			dev_info(&client->dev, "%s: MANUAL SW2,err %d\n", __func__, val);
		val = sm5703_muic_write_reg(client, REG_MANUAL_SW1, SW_ALL_OPEN);
		if (val < 0)
			dev_info(&client->dev, "%s: MANUAL SW1,err %d\n", __func__, val);
	}
}
#endif

int sm5703_muic_vbus_valid_state(void)
{
	struct i2c_client *client;
	int vbus;

	if (!local_usbsw)
		return 0;
	client = local_usbsw->client;

	vbus = sm5703_muic_read_reg(client, REG_VBUSINVALID);
	if (vbus < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, vbus);
		return vbus;
	}
	pr_info("[MUIC] sm5703_muic_vbus_valid_state() vbus = 0x%x\n", vbus);

	return vbus;
}
EXPORT_SYMBOL(sm5703_muic_vbus_valid_state);

static void muic_update_jig_state(struct sm5703_muic_usbsw *usbsw, int dev_type2, int vbus)
{
	if (dev_type2 & DEV_JIG_UART_OFF && !vbus)
		usbsw->attached_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;
	else if (dev_type2 & DEV_JIG_UART_OFF && vbus)
		usbsw->attached_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
	else if (dev_type2 & DEV_JIG_UART_ON)
		usbsw->attached_dev = ATTACHED_DEV_JIG_UART_ON_MUIC;
	else if (dev_type2 & DEV_JIG_USB_OFF)
		usbsw->attached_dev = ATTACHED_DEV_JIG_USB_OFF_MUIC;
	else if (dev_type2 & DEV_JIG_USB_ON)
		usbsw->attached_dev = ATTACHED_DEV_JIG_USB_ON_MUIC;
}

static int sm5703_muic_attach_dev(struct sm5703_muic_usbsw *usbsw)
{
	int adc;
	int val1, val2, val3, val4, vbus, val5;
	int chg_type;
	int control_value, ret;
	struct sm5703_muic_platform_data *pdata = usbsw->pdata;
	struct i2c_client *client = usbsw->client;
#if defined(CONFIG_VIDEO_MHL_V2)
	/* u8 mhl_ret = 0; */
#endif
#if defined(CONFIG_SEC_O7_PROJECT) || defined(CONFIG_SEC_XCOVER3_PROJECT)||defined(CONFIG_SEC_J7_PROJECT)
	if(fullup == NULL){
		fullup = regulator_get(NULL,"BAT_ID_1.8V");
		if(IS_ERR(fullup))
		{
			pr_err("%s:regulator_get failed for BAT_ID_1.8V\n",__func__);
			return 0;
		}
	}
	ret = regulator_enable(fullup);
	if(ret)
		pr_err("%s:BAT_ID_1.8V enable failed (%d)\n",__func__,ret);
	pr_info("%s Enable SM5703 LDO3 for batt_id fullup\n",__func__);
#endif
	val1 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE1);
	if (val1 < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, val1);
		return val1;
	}

	val2 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE2);
	if (val2 < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, val2);
		return val2;
	}
	jig_state =  (val2 & DEV_T2_JIG_ALL_MASK) ? 1 : 0;

	val3 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE3);
	if (val3 < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, val3);
		return val3;
	}

	val4 = sm5703_muic_read_reg(client, REG_CARKIT_STATUS);
	if (val4 < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, val4);
		return val4;
	}
	vbus = sm5703_muic_read_reg(client, REG_VBUSINVALID);
	if (vbus < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, vbus);
		return vbus;
	}
#if defined(CONFIG_S2MM001_VBUS_CHECK)
	vbus |= (val3 & DEV_VBUSIN_VALID);
#endif
	val5 = sm5703_muic_read_reg(client, REG_CHG_TYPE);
	if (val5 < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, val5);
		return val5;
	}
	adc = sm5703_muic_read_reg(client, REG_ADC);
#if !defined(CONFIG_USBID_STANDARD_VER_01)
	switch (adc) {
#if !defined(CONFIG_USB_HOST_NOTIFY)
	case ADC_OTG:
#endif
	case ADC_VZW_DOCK:
	case ADC_VZW_INCOMPATIBLE:
#if !defined(CONFIG_MUIC_SUPPORT_SMARTDOCK)
	case ADC_SMART_DOCK:
#endif
	case ADC_HMT:
#if !defined(CONFIG_USB_HOST_NOTIFY)
	case ADC_AUDIO_DOCK:
#endif
	case ADC_LANHUB:
#if !defined(CONFIG_MUIC_SUPPORT_CHARGING_CABLE)
	case ADC_CHARGING_CABLE:
#endif
	case ADC_MPOS:
#if !defined(CONFIG_MUIC_SUPPORT_DESKDOCK)
	case ADC_DESKDOCK:
#endif
		pr_info("%s,[SM5703_MUIC MUIC] Unsupported Accessory!\n", __func__);
		goto attach_end;
		break;
	}
#endif
#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
	if (adc == ADC_LANHUB) {
		val2 = DEV_LANHUB;
		val1 = 0;
	}
#endif

#if defined(CONFIG_MUIC_SUPPORT_SMARTDOCK)
	if (adc == ADC_SMART_DOCK) {
		val2 = DEV_SMARTDOCK;
		val1 = 0;
	}
#endif
#if defined(CONFIG_USB_HOST_NOTIFY)
	if (adc == 0x11 || adc == ADC_AUDIO_DOCK) {
		val2 = DEV_AUDIO_DOCK;
		val1 = 0;
	}
#endif
	dev_err(&client->dev,
			"dev1: 0x%x, dev2: 0x%x, dev3: 0x%x, Carkit: 0x%x, ADC: 0x%x, vbus:0x%x, Jig: %s, chg_type:0x%x\n",
			val1, val2, val3, val4, adc, vbus,
			(check_sm5703_muic_jig_state() ? "ON" : "OFF"), val5);

	/* USB */
	if ((val1 & DEV_USB) || (val2 & DEV_T2_USB_MASK)
			|| (val3 & DEV_NON_STANDARD)) {
		if (vbus & DEV_VBUSIN_VALID) {
			pr_info("[SM5703_MUIC MUIC] USB Connected\n");
			pdata->callback(CABLE_TYPE_USB, SM5703_MUIC_ATTACHED);
			usbsw->attached_dev = ATTACHED_DEV_USB_MUIC;
		} else {
			pr_info("[SM5703_MUIC MUIC] USB Connected but not VBUS\n");
		}
	/* USB_CDP */
	} else if (val1 & DEV_USB_CHG) {
		pr_info("[MUIC] CDP Connected\n");
		pdata->callback(CABLE_TYPE_CDP, SM5703_MUIC_ATTACHED);
		usbsw->attached_dev = ATTACHED_DEV_CDP_MUIC;
	/* UART */
	} else if (val2 & DEV_T2_UART_MASK) {
		uart_sm5703_muic_connecting = 1;
		muic_update_jig_state(usbsw, val2, vbus);
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
		if (usbsw->is_rustproof) {
			pr_info("[MUIC] RustProof mode, close UART Path\n");
			muic_rustproof_feature(client, SM5703_MUIC_ATTACHED);
			if (vbus & DEV_VBUSIN_VALID)
				pdata->callback(CABLE_TYPE_JIG_UART_OFF_VB, SM5703_MUIC_ATTACHED);
			else
				pdata->callback(CABLE_TYPE_UARTOFF, SM5703_MUIC_ATTACHED);
		} else
#endif
		{
			pr_info("[MUIC] UART Connected\n");
			sm5703_muic_write_reg(client, REG_MANUAL_SW1, SW_UART);
			if (vbus & DEV_VBUSIN_VALID)
				pdata->callback(CABLE_TYPE_JIG_UART_OFF_VB, SM5703_MUIC_ATTACHED);
			else
				pdata->callback(CABLE_TYPE_UARTOFF, SM5703_MUIC_ATTACHED);
		}
	/* L Company Cable Detection Code */
	} else if ((adc == ADC_LCABLE) &&
			(val4 & DEV_CARKIT_CHARGER1_MASK) && (BCD_scan == 0)) {
		pr_info("[MUIC] 219K USB Cable/Charger Connected\n");
		pr_info("[MUIC] BCD rescan\n");

		/* 0x21 -> 1  0x21 -> 0 */
		sm5703_muic_write_reg(client, REG_BCD_RESCAN, 0x01);
		mdelay(1);
		sm5703_muic_write_reg(client, REG_BCD_RESCAN, 0x00);
		BCD_scan = 1;
		return adc;
		/* L Company Cable Detection Code */
	} else if ((adc == ADC_LCABLE) &&
			(val4 & DEV_CARKIT_CHARGER1_MASK) && (BCD_scan == 1)) {
		pr_info("[MUIC] BCD result\n");

		BCD_scan = 0;

		chg_type = sm5703_muic_read_reg(client, REG_CHG_TYPE);
		if (chg_type < 0) {
			dev_err(&client->dev, "%s: err %d\n", __func__, chg_type);
			return chg_type;
		}
		pr_info("[MUIC] chg_type = 0x%x\n", chg_type);
		switch (chg_type) {
		case 0x01: /* DCP */
			pdata->callback(CABLE_TYPE_AC, SM5703_MUIC_ATTACHED);
			break;
		case 0x02: /* CDP */
			pdata->callback(CABLE_TYPE_CDP, SM5703_MUIC_ATTACHED);
			break;
		case 0x04: /* SDP */
			pdata->callback(CABLE_TYPE_USB, SM5703_MUIC_ATTACHED);
			break;
		case 0x08: /* Time out SDP */
			pdata->callback(CABLE_TYPE_219KUSB, SM5703_MUIC_ATTACHED);
			break;
		case 0x10: /* U200 */
			pdata->callback(CABLE_TYPE_AC, SM5703_MUIC_ATTACHED);
			break;
		}
	/* CHARGER */
	} else if ((val1 & DEV_T1_CHARGER_MASK) ||
			(val3 & DEV_T3_CHARGER_MASK)) {
		pr_info("[MUIC] Charger Connected\n");

		/* Quick Charger issue : DP/DM open */
		pr_info("[MUIC] DP/DM open\n");

		ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1, SW_ALL_OPEN_WITHOUT_VBUS);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);

		control_value = sm5703_muic_read_reg(client, REG_CONTROL);
		if (control_value < 0) {
			dev_err(&client->dev, "%s: err %d\n", __func__, control_value);
			return control_value;
		}
		control_value &= ~CON_MANUAL_SW;
		ret = sm5703_muic_write_reg(client, REG_CONTROL, control_value);
		if (ret < 0) {
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
			return ret;
		}

		usbsw->attached_dev = ATTACHED_DEV_TA_MUIC;
		pdata->callback(CABLE_TYPE_AC, SM5703_MUIC_ATTACHED);
#if defined(CONFIG_USB_HOST_NOTIFY)
	/* for SAMSUNG OTG */
	} else if (val1 & DEV_USB_OTG && adc == ADC_OTG) {
		pr_info("[MUIC] OTG Connected\n");
		usbsw->attached_dev = ATTACHED_DEV_OTG_MUIC;
#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
		sm5703_muic_enable_rawdataInterrupts(usbsw);
		usbsw->dock_attached = SM5703_MUIC_ATTACHED;
		usbsw->previous_dock = ADC_OTG;
#endif
		sm5703_muic_set_otg(usbsw, SM5703_MUIC_ATTACHED);
		pdata->callback(CABLE_TYPE_OTG, SM5703_MUIC_ATTACHED);
#endif
	/* JIG */
	} else if (val2 & DEV_T2_JIG_MASK) {
		pr_info("[MUIC] JIG Connected\n");
		muic_update_jig_state(usbsw, val2, vbus);
		pdata->callback(CABLE_TYPE_JIG, SM5703_MUIC_ATTACHED);
#if defined(CONFIG_MUIC_SUPPORT_DESKDOCK)
	/* Desk Dock */
	} else if ((val2 & DEV_AV) || (val3 & DEV_AV_VBUS)) {
		pr_info("[MUIC] Deskdock Connected\n");
		usbsw->attached_dev = ATTACHED_DEV_DESKDOCK_MUIC;
		local_usbsw->dock_attached = SM5703_MUIC_ATTACHED;
		if (vbus & DEV_VBUSIN_VALID)
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_DESK_DOCK,
					SM5703_MUIC_ATTACHED, SW_AUDIO);
		else
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_DESK_DOCK_NO_VB,
					SM5703_MUIC_ATTACHED, SW_AUDIO);
#endif
#if defined(CONFIG_MUIC_SUPPORT_SMARTDOCK)
	/* Smart Dock */
	} else if (val2 & DEV_SMARTDOCK) {
		pr_info("[MUIC] Smartdock Connected\n");
		usbsw->attached_dev = ATTACHED_DEV_SMARTDOCK_MUIC;
		local_usbsw->dock_attached = SM5703_MUIC_ATTACHED;
		if (vbus & DEV_VBUSIN_VALID)
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_SMART_DOCK,
				SM5703_MUIC_ATTACHED, SW_AUDIO);
		else
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_SMART_DOCK_NO_VB,
				SM5703_MUIC_ATTACHED, SW_AUDIO);
#endif
#if defined(CONFIG_VIDEO_MHL_V2)
	/* MHL */
	} else if (val3 & DEV_MHL) {
		pr_info("[MUIC] MHL Connected\n");
		if (!poweroff_charging)
			/* mhl_ret = mhl_onoff_ex(1); support from sii8240 */
		else
			pr_info("LPM mode, skip MHL sequence\n");
#endif
	/* Car Dock */
	} else if (val2 & DEV_JIG_UART_ON) {
		pr_info("[MUIC] Cardock Connected\n");
		muic_update_jig_state(usbsw, val2, vbus);
#if defined(CONFIG_SEC_FACTORY)
		local_usbsw->dock_attached = SM5703_MUIC_ATTACHED;
		pdata->callback(CABLE_TYPE_CARDOCK, SM5703_MUIC_ATTACHED);
#elif defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
		if (usbsw->is_rustproof) {
			pr_info("[MUIC] RustProof mode, close UART Path\n");
			muic_rustproof_feature(client, SM5703_MUIC_ATTACHED);
		}
		pdata->callback(CABLE_TYPE_UARTON, SM5703_MUIC_ATTACHED);
#else
		pdata->callback(CABLE_TYPE_UARTON, SM5703_MUIC_ATTACHED);
#endif
#if defined(CONFIG_USB_HOST_NOTIFY)
	/* Audio Dock */
	} else if (val2 & DEV_AUDIO_DOCK) {
		pr_info("[MUIC] Audiodock Connected\n");
		usbsw->attached_dev = ATTACHED_DEV_AUDIODOCK_MUIC;
		sm5703_muic_dock_control(usbsw, CABLE_TYPE_AUDIO_DOCK,
				SM5703_MUIC_ATTACHED, SW_DHOST);
#endif

#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
	/* LANHUB */
	} else if (val2 & DEV_LANHUB) {
		if (usbsw->previous_dock == ADC_LANHUB &&
				usbsw->lanhub_ta_status == 1) {
			/* Skip for the LANHUB+TA Case */
			pr_info("[MUIC] Lanhub + TA is connected\n");
		} else {
		/* Enable RAWDATA Interrupts */
			sm5703_muic_enable_rawdataInterrupts(usbsw);
			sm5703_muic_detect_lanhub(usbsw);
		}
#endif
#if defined(CONFIG_MUIC_SUPPORT_CHARGING_CABLE)
		/* Charging Cable */
	} else if (adc == ADC_CHARGING_CABLE) {
		pr_info("[MUIC] Charging Cable Connected\n");
		usbsw->attached_dev = ATTACHED_DEV_CHARGING_CABLE_MUIC;
		pdata->callback(CABLE_TYPE_CHARGING_CABLE,
			SM5703_MUIC_ATTACHED);
#endif
#if defined(CONFIG_MUIC_SUPPORT_VZW_INCOMPATIBLE)
	/* Incompatible Charger */
	} else if (vbus & DEV_VBUSIN_VALID && ((adc == ADC_VZW_INCOMPATIBLE) || (adc == ADC_VZW_DOCK))) {
		pr_info("[MUIC] Incompatible Charger Connected\n");
		usbsw->attached_dev = ATTACHED_DEV_UNKNOWN_MUIC;
		pdata->callback(CABLE_TYPE_INCOMPATIBLE,
			SM5703_MUIC_ATTACHED);
#endif
	/* Undefined */
	} else if (vbus & DEV_VBUSIN_VALID) {
		pr_info("[MUIC] Undefined Charger Connected\n");
		usbsw->attached_dev = ATTACHED_DEV_UNKNOWN_MUIC;
		pdata->callback(CABLE_TYPE_UNDEFINED,
				SM5703_MUIC_ATTACHED);
		usbsw->undefined_attached = true;
	}
#if defined(CONFIG_MUIC_NOTI)
	muic_notifier_attach_attached_dev(usbsw->attached_dev);
#endif
#if !defined(CONFIG_USBID_STANDARD_VER_01)
attach_end:
#endif
	usbsw->dev1 = val1;
	usbsw->dev2 = val2;
	usbsw->dev3 = val3;
	usbsw->adc = adc;
	usbsw->vbus = vbus;
	usbsw->carkit_dev = val4;

	return adc;
}

static int sm5703_muic_detach_dev(struct sm5703_muic_usbsw *usbsw)
{
	struct sm5703_muic_platform_data *pdata = usbsw->pdata;
	int control_value, ret;
	pr_info("%s\n", __func__);
	pr_info("dev1: 0x%x,dev2: 0x%x,chg_typ: 0x%x,vbus %d,ADC: 0x%x\n",
			usbsw->dev1, usbsw->dev2, usbsw->dev3, usbsw->vbus, usbsw->adc);

#if defined(CONFIG_SEC_O7_PROJECT) || defined(CONFIG_SEC_XCOVER3_PROJECT)||defined(CONFIG_SEC_J7_PROJECT)
	if(fullup == NULL){
		fullup = regulator_get(NULL,"BAT_ID_1.8V");
		if(IS_ERR(fullup))
		{
			pr_err("%s:regulator_get failed for BAT_ID_1.8V\n",__func__);
			return 0;
		}
	}
	ret = regulator_disable(fullup);
	if(ret)
		pr_err("%s:BAT_ID_1.8V disable failed (%d)\n",__func__,ret);
	pr_info("%s Disable SM5703 LDO3\n",__func__);
#endif
#if !defined(CONFIG_USBID_STANDARD_VER_01)
	switch (usbsw->adc) {
#if !defined(CONFIG_USB_HOST_NOTIFY)
	case ADC_OTG:
#endif
	case (ADC_VZW_DOCK)...(ADC_MPOS):
	case ADC_DESKDOCK:
		pr_info("%s,[SM5703 MUIC] Unsupported Accessory!\n", __func__);
		goto detach_end;
		break;
	}
#endif
	/* USB */
	if ((usbsw->dev1 & DEV_USB) || (usbsw->dev2 & DEV_T2_USB_MASK)
			|| (usbsw->dev3 & DEV_NON_STANDARD)) {
		pr_info("[MUIC] USB Disonnected\n");
		pdata->callback(CABLE_TYPE_USB, SM5703_MUIC_DETACHED);
	} else if (usbsw->dev1 & DEV_USB_CHG) {
		pdata->callback(CABLE_TYPE_CDP, SM5703_MUIC_DETACHED);

	/* UART */
	} else if (usbsw->dev2 & DEV_T2_UART_MASK) {
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
		if (usbsw->is_rustproof) {
			pr_info("[MUIC] RustProof mode Disconnected Event\n");
			muic_rustproof_feature(usbsw->client, SM5703_MUIC_DETACHED);
			if (usbsw->vbus & DEV_VBUSIN_VALID)
				pdata->callback(CABLE_TYPE_JIG_UART_OFF_VB, SM5703_MUIC_DETACHED);
			else
				pdata->callback(CABLE_TYPE_UARTOFF, SM5703_MUIC_DETACHED);
		} else
#endif
		{
			pr_info("[MUIC] UART Disconnected\n");
			if (usbsw->vbus & DEV_VBUSIN_VALID)
				pdata->callback(CABLE_TYPE_JIG_UART_OFF_VB, SM5703_MUIC_DETACHED);
			else
				pdata->callback(CABLE_TYPE_UARTOFF, SM5703_MUIC_DETACHED);
			uart_sm5703_muic_connecting = 0;
		}
	/* L Company Cable Detection Code */
	} else if (usbsw->adc == ADC_LCABLE &&
			(usbsw->carkit_dev & DEV_CARKIT_CHARGER1_MASK)) {
		pr_info("[MUIC] 219K USB Cable/Charger Disconnected\n");
		pdata->callback(CABLE_TYPE_219KUSB, SM5703_MUIC_DETACHED);
	/* CHARGER */
	} else if ((usbsw->dev1 & DEV_T1_CHARGER_MASK) ||
			(usbsw->dev3 & DEV_T3_CHARGER_MASK)) {
		pr_info("[MUIC] Charger Disconnected\n");

		/* Quick Charger issue : DP/DM open */
		ret = sm5703_muic_write_reg(usbsw->client, REG_MANUAL_SW1, SW_ALL_OPEN);
		if (ret < 0)
			dev_err(&usbsw->client->dev, "%s: err %d\n", __func__, ret);
		control_value = sm5703_muic_read_reg(usbsw->client, REG_CONTROL);
		if (control_value < 0)
			dev_err(&usbsw->client->dev, "%s: err %d\n", __func__, control_value);
		control_value &= CON_MANUAL_SW;
		ret = sm5703_muic_write_reg(usbsw->client, REG_CONTROL, control_value);
		if (ret < 0)
			dev_err(&usbsw->client->dev, "%s: err %d\n", __func__, ret);

		pdata->callback(CABLE_TYPE_AC, SM5703_MUIC_DETACHED);
#if defined(CONFIG_USB_HOST_NOTIFY)
	/* for SAMSUNG OTG */
	} else if (usbsw->dev1 & DEV_USB_OTG) {
		pr_info("[MUIC] OTG Disconnected\n");
#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
		sm5703_muic_disable_rawdataInterrupts(usbsw);
		pr_info("%s:lanhub_ta_status(%d)\n",
				__func__, usbsw->lanhub_ta_status);
		lanhub_ta_case = false;
		if (usbsw->lanhub_ta_status == 0) {
			pdata->callback(CABLE_TYPE_OTG, SM5703_MUIC_DETACHED);
			sm5703_muic_set_otg(usbsw, SM5703_MUIC_DETACHED);
		} else if (pdata->lanhub_cb && usbsw->lanhub_ta_status == 1) {
			pdata->lanhub_cb(CABLE_TYPE_LANHUB, SM5703_MUIC_DETACHED, LANHUB);
		}
		usbsw->dock_attached = SM5703_MUIC_DETACHED;
		usbsw->lanhub_ta_status = 0;
#else
		sm5703_muic_set_otg(usbsw, SM5703_MUIC_DETACHED);
		pdata->callback(CABLE_TYPE_OTG, SM5703_MUIC_DETACHED);
#endif
#endif
	/* JIG */
	} else if (usbsw->dev2 & DEV_T2_JIG_MASK) {
		pr_info("[MUIC] JIG Disconnected\n");
		pdata->callback(CABLE_TYPE_JIG, SM5703_MUIC_DETACHED);
#if defined(CONFIG_MUIC_SUPPORT_DESKDOCK)
	/* Desk Dock */
	} else if ((usbsw->dev2 & DEV_AV) ||
			(usbsw->dev3 & DEV_AV_VBUS)) {
		pr_info("[MUIC] Deskdock Disonnected\n");
		local_usbsw->dock_attached = SM5703_MUIC_DETACHED;
		if (usbsw->vbus & DEV_VBUSIN_VALID)
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_DESK_DOCK,
					SM5703_MUIC_DETACHED, SW_ALL_OPEN);
		else
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_DESK_DOCK_NO_VB,
					SM5703_MUIC_DETACHED, SW_ALL_OPEN);
#endif
#if defined(CONFIG_MUIC_SUPPORT_SMARTDOCK)
	/* Smart Dock */
	} else if (usbsw->dev2 & DEV_SMARTDOCK) {
		pr_info("[MUIC] Smartdock Disconnected\n");
		local_usbsw->dock_attached = SM5703_MUIC_DETACHED;
		if (usbsw->vbus & DEV_VBUSIN_VALID)
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_SMART_DOCK,
				SM5703_MUIC_DETACHED, SW_ALL_OPEN);
		else
			sm5703_muic_dock_control(usbsw, CABLE_TYPE_SMART_DOCK_NO_VB,
				SM5703_MUIC_DETACHED, SW_ALL_OPEN);
#endif
#if defined(CONFIG_MHL_D3_SUPPORT)
	/* MHL */
	} else if (usbsw->dev3 & DEV_MHL) {
		pr_info("[MUIC] MHL Disconnected\n");
		detached_sm5703_muic_status = 1;
#endif
	/* Car Dock */
	} else if (usbsw->dev2 & DEV_JIG_UART_ON) {
		pr_info("[MUIC] Cardock Disconnected\n");
#if defined(CONFIG_SEC_FACTORY)
		local_usbsw->dock_attached = SM5703_MUIC_DETACHED;
		pdata->callback(CABLE_TYPE_CARDOCK, SM5703_MUIC_DETACHED);
#elif defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
		if (usbsw->is_rustproof) {
			pr_info("[MUIC] RustProof mode disconneted Event\n");
			muic_rustproof_feature(usbsw->client, SM5703_MUIC_DETACHED);
		}
		pdata->callback(CABLE_TYPE_UARTON, SM5703_MUIC_DETACHED);
#else
		pdata->callback(CABLE_TYPE_UARTON, SM5703_MUIC_DETACHED);
#endif
#if defined(CONFIG_USB_HOST_NOTIFY)
	/* Audio Dock */
	} else if (usbsw->dev2 == DEV_AUDIO_DOCK) {
		pr_info("[MUIC] Audiodock Disconnected\n");
		sm5703_muic_dock_control(usbsw, CABLE_TYPE_AUDIO_DOCK, SM5703_MUIC_DETACHED, SW_ALL_OPEN);
#endif
#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
	/* LANHUB */
	} else if (usbsw->adc == ADC_LANHUB) {
		pr_info("[MUIC] Lanhub disconnected\n");
		/* Disable RAWDATA Interrupts */
		sm5703_muic_disable_rawdataInterrupts(usbsw);
		lanhub_ta_case = false;
		usbsw->dock_attached = SM5703_MUIC_DETACHED;
		sm5703_muic_set_lanhub(usbsw, SM5703_MUIC_DETACHED);
		sm5703_muic_mask_vbus_detect(usbsw, SM5703_MUIC_DETACHED);
		pr_info("%s:lanhub_ta_status(%d)\n",
				__func__, usbsw->lanhub_ta_status);
		if (pdata->lanhub_cb && usbsw->lanhub_ta_status == 1) {
			pdata->lanhub_cb(CABLE_TYPE_LANHUB, SM5703_MUIC_DETACHED, LANHUB);
		} else if (usbsw->lanhub_ta_status == 0) {
			pdata->callback(CABLE_TYPE_OTG, SM5703_MUIC_DETACHED);
			sm5703_muic_set_otg(usbsw, SM5703_MUIC_DETACHED);
		}
		usbsw->lanhub_ta_status = 0;
		usbsw->dock_attached = SM5703_MUIC_DETACHED;

#endif
#if defined(CONFIG_MUIC_SUPPORT_CHARGING_CABLE)
	/* Charging Cable */
	} else if (usbsw->adc == ADC_CHARGING_CABLE) {
		pr_info("[MUIC] Charging Cable Disconnected\n");
		pdata->callback(CABLE_TYPE_CHARGING_CABLE,
			SM5703_MUIC_DETACHED);
#endif
#if defined(CONFIG_MUIC_SUPPORT_VZW_INCOMPATIBLE)
	/* Incompatible Charger */
	} else if ((usbsw->adc == ADC_VZW_INCOMPATIBLE) || (usbsw->adc == ADC_VZW_DOCK)) {
		pr_info("[MUIC] Incompatible Charger Disconnected\n");
		pdata->callback(CABLE_TYPE_INCOMPATIBLE,
			SM5703_MUIC_DETACHED);
#endif
	/* Undefined */
	} else if (usbsw->undefined_attached) {
		pr_info("[MUIC] Undefined Charger Disconnected\n");
		pdata->callback(CABLE_TYPE_UNDEFINED,
				SM5703_MUIC_DETACHED);
		usbsw->undefined_attached = false;
	}
#if defined(CONFIG_MUIC_NOTI)
	muic_notifier_detach_attached_dev(usbsw->attached_dev);	
#endif
#if !defined(CONFIG_USBID_STANDARD_VER_01)
detach_end:
#endif
	sm5703_muic_write_reg(usbsw->client, REG_CONTROL, CON_MASK);
	sm5703_muic_write_reg(usbsw->client, REG_MANUAL_SW2, 0x00);
	BCD_scan = 0;

	usbsw->dev1 = 0;
	usbsw->dev2 = 0;
	usbsw->dev3 = 0;
	usbsw->adc = 0;
	usbsw->vbus = 0;
	usbsw->carkit_dev = 0;
	usbsw->attached_dev = ATTACHED_DEV_NONE_MUIC;
	return 0;

}
static irqreturn_t sm5703_muic_irq_thread(int irq, void *data)
{
	struct sm5703_muic_usbsw *usbsw = data;
	struct i2c_client *client = usbsw->client;
	int intr1, intr2;
	int val1, val2, val3, adc , vbus;
#ifdef CONFIG_USB_HOST_NOTIFY
	struct otg_notify *n = get_otg_notify();
#endif

	/* SM5703 MUIC : Read interrupt -> Read Device */
	pr_info("sm5703_muic_irq_thread is called\n");
	/* to avoid trash ovp irq */
	msleep(100);
	mutex_lock(&usbsw->mutex);
	intr1 = sm5703_muic_read_reg(client, REG_INT1);
	intr2 = sm5703_muic_read_reg(client, REG_INT2);

	adc = sm5703_muic_read_reg(client, REG_ADC);
	dev_info(&client->dev, "%s:intr1 : 0x%02x, intr2:0x%02x, adc:0x%02x\n",
			__func__, intr1, intr2, adc);

	/* MUIC OVP Check */
	if (intr1 & INT_OVP_ENABLE)
		usbsw->pdata->oxp_callback(ENABLE);
	if (intr1 & INT_OVP_DISABLE)
		usbsw->pdata->oxp_callback(DISABLE);

	/* Workaround for MUIC abnormal reset when Surge testing */
	if ((intr1 & INT_OVP_ENABLE) || ((intr1 == 0) && (intr2 == 0))) {
		int ctrl;
		ctrl = sm5703_muic_read_reg(client, REG_CONTROL);
		pr_info("%s, REG_CONTROL : 0x%x\n", __func__, ctrl);
		if (ctrl == 0x1F) {
			pr_info("%s, sm5703_reg_init is called\n", __func__);
			sm5703_muic_reg_init(usbsw);
		}
	}

#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
	if ((intr1 == 0x00 && (intr2 & 0x04))
			|| ((intr1 & INT_DETACH) && (intr2 & 0x04)))
		sm5703_muic_detect_lanhub(usbsw);
	else
#endif
	/* device detection */
	/* interrupt both attach and detach */
	if (intr1 == (INT_ATTACH + INT_DETACH)) {
		val1 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE1);
		val3 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE3);
		vbus = sm5703_muic_read_reg(client, REG_VBUSINVALID);

		if ((adc == ADC_OPEN) && (val1 == DATA_NONE) &&
				((val3 == DATA_NONE) || (vbus == 0x00)))
			sm5703_muic_detach_dev(usbsw);
		else
			sm5703_muic_attach_dev(usbsw);

	/* interrupt attach */
	} else if ((intr1 & INT_ATTACH) || (intr2 & INT_RESERVED_ATTACH) || (intr2 & INT_MHL)) {
			sm5703_muic_attach_dev(usbsw);
	/* BCD recan */
	} else if ((intr2 & INT_RID_CHARGER) && (BCD_scan == 1)) {
			sm5703_muic_attach_dev(usbsw);
	/* interrupt detach */
	} else if (intr1 & INT_DETACH) {
		sm5703_muic_detach_dev(usbsw);
	} else if (intr2 == INT_VBUSDET_ON) {
		pr_info("sm5703 muic: VBUSOUT_ON\n");
#ifdef CONFIG_USB_HOST_NOTIFY
		send_otg_notify(n, NOTIFY_EVENT_VBUSPOWER, 1);
#endif
		val1 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE1);
		val2 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE2);
		val3 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE3);
		vbus = sm5703_muic_read_reg(client, REG_VBUSINVALID);

		pr_info("sm5703 muic: dev1=0x%x, dev2=0x%x, dev3=0x%x, vbus=0x%x\n",
				val1, val2, val3, vbus);
#ifdef CONFIG_USB_HOST_NOTIFY
		if ((((adc != ADC_OPEN) || (val1 != 0x00 || val2 != 0x00 || val3 != 0x00)) &&
					(adc != ADC_OTG)) && (get_usb_mode() != NOTIFY_TEST_MODE)) {
#else
		if (((adc != ADC_OPEN) || (val1 != 0x00 || val2 != 0x00 || val3 != 0x00))
					&& (get_usb_mode() != NOTIFY_TEST_MODE)) {
#endif
			if (val2 != 0x10)
				sm5703_muic_attach_dev(usbsw);
		} else {
			goto irq_end;
		}
	} else if (intr2 == INT_VBUS_OFF) {
		pr_info("sm5703 muic: VBUSOUT_OFF\n");
#ifdef CONFIG_USB_HOST_NOTIFY
		send_otg_notify(n, NOTIFY_EVENT_VBUSPOWER, 0);
#endif
		val1 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE1);
		val2 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE2);
		val3 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE3);
		vbus = sm5703_muic_read_reg(client, REG_VBUSINVALID);

		pr_info("sm5703 muic: dev1=0x%x, dev2=0x%x, dev3=0x%x, vbus=0x%x \n",
				val1,val2,val3,vbus);

		if (get_usb_mode() != NOTIFY_TEST_MODE) {
	        if (usbsw->attached_dev == ATTACHED_DEV_UNKNOWN_MUIC)
		        sm5703_muic_detach_dev(usbsw);
	        else if (adc != ADC_OPEN)
		        sm5703_muic_attach_dev(usbsw);
	        else if (intr1 != INT_OVP_ENABLE)
		        sm5703_muic_detach_dev(usbsw);
		} else {
			goto irq_end;
		}
	}
irq_end:
	mutex_unlock(&usbsw->mutex);
	pr_info("sm5703_muic_irq_thread,end\n");
	return IRQ_HANDLED;
}

static int sm5703_muic_irq_init(struct sm5703_muic_usbsw *usbsw)
{
	struct i2c_client *client = usbsw->client;
	int ret;

	if (client->irq) {
		ret = request_threaded_irq(client->irq, NULL,
				sm5703_muic_irq_thread, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"sm5703 muic micro USB", usbsw);
		if (ret) {
			dev_err(&client->dev, "failed to reqeust IRQ\n");
			return ret;
		}
		enable_irq_wake(client->irq);
	}

	return 0;
}

static void sm5703_muic_init_detect(struct work_struct *work)
{
	struct sm5703_muic_usbsw *usbsw = container_of(work,
			struct sm5703_muic_usbsw, init_work.work);
	int ret;
	int int_reg1, int_reg2;

	dev_info(&usbsw->client->dev, "%s\n", __func__);

	/* read Interrupt register twice times */
	int_reg1 = sm5703_muic_read_reg(usbsw->client, REG_INT1);
	dev_info(&usbsw->client->dev, "%s: intr1 : 0x%x\n",
			__func__, int_reg1);
	int_reg2 = sm5703_muic_read_reg(usbsw->client, REG_INT2);
	dev_info(&usbsw->client->dev, "%s: intr2 : 0x%x\n",
			__func__, int_reg2);
	int_reg1 = sm5703_muic_read_reg(usbsw->client, REG_INT1);
	dev_info(&usbsw->client->dev, "%s: intr1 : 0x%x\n",
			__func__, int_reg1);
	int_reg2 = sm5703_muic_read_reg(usbsw->client, REG_INT2);
	dev_info(&usbsw->client->dev, "%s: intr2 : 0x%x\n",
			__func__, int_reg2);

	mutex_lock(&usbsw->mutex);
	sm5703_muic_attach_dev(usbsw);
	mutex_unlock(&usbsw->mutex);

	ret = sm5703_muic_irq_init(usbsw);
	if (ret)
		dev_info(&usbsw->client->dev,
				"failed to enable  irq init %s\n", __func__);
}

#ifdef CONFIG_OF
static int sm5703_muic_parse_dt(struct device *dev, struct sm5703_muic_platform_data *pdata)
{

	struct device_node *np = dev->of_node;

	/* reset, irq gpio info */
	pdata->gpio_scl = of_get_named_gpio_flags(np, "sm5703,gpio-scl",
			0, &pdata->scl_gpio_flags);
	pdata->gpio_uart_on = of_get_named_gpio_flags(np, "sm5703,uarton-gpio",
			0, &pdata->uarton_gpio_flags);
	pdata->gpio_sda = of_get_named_gpio_flags(np, "sm5703,gpio-sda",
			0, &pdata->sda_gpio_flags);
	pdata->gpio_int = of_get_named_gpio_flags(np, "sm5703,irq-gpio",
			0, &pdata->irq_gpio_flags);
	pr_info("%s: irq-gpio: %u\n", __func__, pdata->gpio_int);

	return 0;
}
#endif

static int sm5703_muic_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct sm5703_muic_usbsw *usbsw;
	int ret = 0;
	struct device *switch_dev;
	struct sm5703_muic_platform_data *pdata;
	struct pinctrl *muic_pinctrl;

	dev_info(&client->dev, "%s:sm5703 muic probe called\n", __func__);
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(struct sm5703_muic_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
		ret = sm5703_muic_parse_dt(&client->dev, pdata);
		if (ret < 0)
			return ret;

		pdata->callback = sm5703_muic_callback;
#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
		pdata->lanhub_cb = sm5703_muic_lanhub_callback;
#endif
		pdata->dock_init = sm5703_muic_dock_init;
		pdata->oxp_callback = sm5703_muic_oxp_callback;
		pdata->mhl_sel = NULL;

		muic_pinctrl = devm_pinctrl_get_select(&client->dev, "sm5703_i2c_active");
		if (IS_ERR(muic_pinctrl)) {
			if (PTR_ERR(muic_pinctrl) == -EPROBE_DEFER)
				return -EPROBE_DEFER;

			pr_debug("Target does not use pinctrl\n");
			muic_pinctrl = NULL;
		}

#if defined(CONFIG_GPIO_MSM_V3)
		gpio_tlmm_config(GPIO_CFG(pdata->gpio_int,  0, GPIO_CFG_INPUT,
					GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_tlmm_config(GPIO_CFG(pdata->gpio_scl,  0, GPIO_CFG_INPUT,
					GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_tlmm_config(GPIO_CFG(pdata->gpio_sda,  0, GPIO_CFG_INPUT,
					GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_tlmm_config(GPIO_CFG(pdata->gpio_uart_on,  0, GPIO_CFG_INPUT,
					GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_DISABLE);
#endif
		client->irq = gpio_to_irq(pdata->gpio_int);
	} else
		pdata = client->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	usbsw = kzalloc(sizeof(struct sm5703_muic_usbsw), GFP_KERNEL);
	if (!usbsw) {
		dev_err(&client->dev, "failed to allocate driver data\n");
		kfree(usbsw);
		return -ENOMEM;
	}

	usbsw->client = client;
	if (client->dev.of_node)
		usbsw->pdata = pdata;
	else
		usbsw->pdata = client->dev.platform_data;
	if (!usbsw->pdata)
		goto fail1;

	i2c_set_clientdata(client, usbsw);

	mutex_init(&usbsw->mutex);
	usbsw->undefined_attached = false;
	local_usbsw = usbsw;

#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
	ret = sm5703_muic_read_reg(client, REG_MANUAL_SW1);
	if (ret < 0)
		dev_err(&client->dev, "failed to read MANUAL SW1 Reg, err:%d\n",
				ret);
	/* Keep the feature disabled by default */
	usbsw->is_rustproof = false;
	/* RUSTPROOF: disable UART connection
	 * if MANSW1 from BL is OPEN_RUSTPROOF */
	if (ret == MANSW1_OPEN_RUSTPROOF)
		usbsw->is_rustproof = true;

#endif
	sm5703_muic_reg_init(usbsw);

	ret = sysfs_create_group(&client->dev.kobj, &sm5703_muic_group);
	if (ret) {
		dev_err(&client->dev,
				"failed to create sm5703 muic attribute group\n");
		goto fail2;
	}

	/* make sysfs node /sys/class/sec/switch/usb_state */
	switch_dev = device_create(sec_class, NULL, 0, NULL, "switch");
	if (IS_ERR(switch_dev)) {
		pr_err("[SM5703 MUIC] Failed to create device (switch_dev)!\n");
		ret = PTR_ERR(switch_dev);
		goto fail2;
	}

	ret = device_create_file(switch_dev, &dev_attr_usb_state);
	if (ret < 0) {
		pr_err("[SM5703 MUIC] Failed to create file (usb_state)!\n");
		goto err_create_file_state;
	}

	ret = device_create_file(switch_dev, &dev_attr_adc);
	if (ret < 0) {
		pr_err("[SM5703 MUIC] Failed to create file (adc)!\n");
		goto err_create_file_adc;
	}

	ret = device_create_file(switch_dev, &dev_attr_reset_switch);
	if (ret < 0) {
		pr_err("[SM5703 MUIC] Failed to create file (reset_switch)!\n");
		goto err_create_file_reset_switch;
	}

	ret = device_create_file(switch_dev, &dev_attr_attached_dev);
	if (ret < 0) {
		pr_err("[SM5703 MUIC] Failed to create file (attached_dev)!\n");
		goto err_create_file_attached_dev;
	}

#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
	ret = device_create_file(switch_dev, &dev_attr_uart_en);
	if (ret < 0) {
		pr_err("[SM5703 MUIC] Failed to create file (uart_en)!\n");
		goto err_create_file_uart_en;
	}
	ret = device_create_file(switch_dev, &dev_attr_uart_sel);
	if (ret < 0) {
		pr_err("[SM5703 MUIC] Failed to create file (uart_sel)!\n");
		goto err_create_file_uart_sel;
	}
	ret = device_create_file(switch_dev, &dev_attr_usb_sel);
	if (ret < 0) {
		pr_err("[SM5703 MUIC] Failed to create file (usb_sel)!\n");
		goto err_create_file_usb_sel;
	}

#endif
	local_usbsw->attached_dev = ATTACHED_DEV_NONE_MUIC;
#if defined(CONFIG_MUIC_SM5703_SUPPORT_LANHUB_TA)
	local_usbsw->previous_dock = SM5703_MUIC_NONE;
	local_usbsw->lanhub_ta_status = 0;
#endif
	dev_set_drvdata(switch_dev, usbsw);
	/* sm5703 muic dock init */
	if (usbsw->pdata->dock_init)
		usbsw->pdata->dock_init();

	/* initial cable detection */
	INIT_DELAYED_WORK(&usbsw->init_work, sm5703_muic_init_detect);
	schedule_delayed_work(&usbsw->init_work, msecs_to_jiffies(2700));

	return 0;

#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF)
err_create_file_usb_sel:
	device_remove_file(switch_dev, &dev_attr_usb_sel);
err_create_file_uart_sel:
	device_remove_file(switch_dev, &dev_attr_uart_sel);
err_create_file_uart_en:
	device_remove_file(switch_dev, &dev_attr_uart_en);
#endif
err_create_file_attached_dev:
	device_remove_file(switch_dev, &dev_attr_attached_dev);
err_create_file_reset_switch:
	device_remove_file(switch_dev, &dev_attr_reset_switch);
err_create_file_adc:
	device_remove_file(switch_dev, &dev_attr_adc);
err_create_file_state:
	device_remove_file(switch_dev, &dev_attr_usb_state);
fail2:
	if (client->irq)
		free_irq(client->irq, usbsw);
fail1:
	mutex_destroy(&usbsw->mutex);
	i2c_set_clientdata(client, NULL);
	kfree(usbsw);
	return ret;
}

static int sm5703_muic_remove(struct i2c_client *client)
{
	struct sm5703_muic_usbsw *usbsw = i2c_get_clientdata(client);
	cancel_delayed_work(&usbsw->init_work);
	if (client->irq) {
		disable_irq_wake(client->irq);
		free_irq(client->irq, usbsw);
	}
	mutex_destroy(&usbsw->mutex);
	i2c_set_clientdata(client, NULL);

	sysfs_remove_group(&client->dev.kobj, &sm5703_muic_group);
	kfree(usbsw);
	return 0;
}
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF_INBATT)
static int sm5703_muic_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sm5703_muic_usbsw *usbsw = i2c_get_clientdata(client);
	int ret;

	usbsw->mansw = sm5703_muic_read_reg(client, REG_MANUAL_SW1);
	ret = sm5703_muic_write_reg(client,
			REG_MANUAL_SW1, SW_ALL_OPEN_WITHOUT_VBUS);
	dev_err(&client->dev, "%s: into suspend %d\n", __func__, ret);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	ret = sm5703_muic_read_reg(client, REG_CONTROL);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	else
		ret = sm5703_muic_write_reg(client,
				REG_CONTROL, ret & ~CON_MANUAL_SW);
	return 0;
}
#endif
static int sm5703_muic_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sm5703_muic_usbsw *usbsw = i2c_get_clientdata(client);
	int ldev1, ldev2, ldev3, intr1, intr2, vbus;
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF_INBATT)
	int ret;
#endif
	pr_info("%s: resume\n", __func__);
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF_INBATT)
	ret = sm5703_muic_write_reg(client, REG_MANUAL_SW1, usbsw->mansw);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	ret = sm5703_muic_read_reg(client, REG_CONTROL);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	else
		ret = sm5703_muic_write_reg(client, REG_CONTROL,
				ret | CON_MANUAL_SW);
#endif
	ldev1 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE1);
	if (ldev1 < 0) {
		pr_err("%s: Dev reg 1 read err! %d\n", __func__, ldev1);
		goto safe_exit;
	}
	ldev2 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE2);
	if (ldev2 < 0) {
		pr_err("%s: Dev reg 2 read err! %d\n", __func__, ldev2);
		goto safe_exit;
	}
	ldev3 = sm5703_muic_read_reg(client, REG_DEVICE_TYPE3);
	if (ldev3 < 0) {
		pr_err("%s: Dev reg 3 read err! %d\n", __func__, ldev3);
		goto safe_exit;
	}
	vbus = sm5703_muic_read_reg(client, REG_VBUSINVALID);
	if (vbus < 0) {
		pr_err("%s: vbus  read err! %d\n", __func__, vbus);
		goto safe_exit;
	}

	intr1 = sm5703_muic_read_reg(client, REG_INT1);
	intr2 = sm5703_muic_read_reg(client, REG_INT2);
	pr_info("%s: intr1=0x%x, intr2=0x%x, ldev1=0x%x, ldev2=0x%x, ldev3=0x%x, vbus=0x%x\n",
			__func__, intr1, intr2, ldev1, ldev2, ldev3, vbus);

	if (intr1 & INT_DETACH) {
		mutex_lock(&usbsw->mutex);
		sm5703_muic_detach_dev(usbsw);
		mutex_unlock(&usbsw->mutex);
	} else if ((usbsw->dev1 != ldev1) || (usbsw->dev2 != ldev2) ||
			(usbsw->dev3 != ldev3) || (usbsw->vbus != vbus)) {
		/* device detection */
		mutex_lock(&usbsw->mutex);
		sm5703_muic_attach_dev(usbsw);
		mutex_unlock(&usbsw->mutex);
	}

safe_exit:
	return 0;
}

static const struct dev_pm_ops sm5703_muic_pm_ops = {
#if defined(CONFIG_MUIC_SUPPORT_RUSTPROOF_INBATT)
	.suspend = sm5703_muic_suspend,
#endif
	.resume = sm5703_muic_resume,
};

static const struct i2c_device_id sm5703_muic_id[] = {
	{"sm5703", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sm5703_muic_id);
static struct of_device_id sm5703_muic_i2c_match_table[] = {
	{ .compatible = "sm5703,i2c",},
	{},
};
MODULE_DEVICE_TABLE(of, sm5703_muic_i2c_match_table);

static struct i2c_driver sm5703_muic_i2c_driver = {
	.driver = {
		.name = "sm5703",
		.owner = THIS_MODULE,
		.of_match_table = sm5703_muic_i2c_match_table,
		.pm = &sm5703_muic_pm_ops,
	},
	.probe  = sm5703_muic_probe,
	.remove = sm5703_muic_remove,
	.id_table = sm5703_muic_id,
};

static int __init sm5703_muic_init(void)
{
	return i2c_add_driver(&sm5703_muic_i2c_driver);
}
/*
 * Late init call is required MUIC was accessing
 * USB driver before USB initialization and watch dog reset
 * was happening when booted with USB connected */
late_initcall(sm5703_muic_init);

static void __exit sm5703_muic_exit(void)
{
	i2c_del_driver(&sm5703_muic_i2c_driver);
}
module_exit(sm5703_muic_exit);

MODULE_AUTHOR("Jeongrae Kim <Jryu.kim@samsung.com>");
MODULE_DESCRIPTION("SM5703 MUIC Micro USB Switch driver");
MODULE_LICENSE("GPL");


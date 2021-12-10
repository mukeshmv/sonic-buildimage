#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include "fpga_i2c_mux.h"


#define NUM_I2C_MUX_CHANS 4
#define I2C_MUX_CHAN_BASE_ID 128 

struct fpga_i2c_mux_data {
	struct i2c_client *client;
	void __iomem *mux_reg_addr; 	
	u32 last_chan;
};

static int fpga_i2c_mux_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct fpga_i2c_mux_data *data = i2c_mux_priv(muxc);
	/*struct i2c_client *client = data->client;
	int err = 0;
*/
	data->last_chan = chan;
	iowrite32(chan, data->mux_reg_addr);
	return 0;
}

static int fpga_i2c_mux_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	struct fpga_i2c_mux_data *data = i2c_mux_priv(muxc);
	//struct i2c_client *client = data->client;

	/* Only select the channel if its different from the last channel */
	if (data->last_chan != chan) {	
		iowrite32(chan, data->mux_reg_addr);
		data->last_chan = chan;
	}
	return 0;
}


static ssize_t show_i2c_mux_select(struct device *dev,
				struct device_attribute *devattr,
				char *buf)
{
	struct fpga_i2c_mux_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%02X\n", ioread32(data->mux_reg_addr) & 0x7);
}

static ssize_t store_i2c_mux_select(struct device *dev,
				struct device_attribute *devattr,
				const char *buf,
				size_t len)
{
	struct fpga_i2c_mux_data *data = dev_get_drvdata(dev);
	unsigned long chan;
	int ret;

	ret = kstrtoul(buf, 0, &chan);
	if (ret < 0)
		return ret;

	if(chan >= 0 && chan <= 3) {
		iowrite32(chan, data->mux_reg_addr);
	} else {
		return -EINVAL;
	}
	return len;
}

static DEVICE_ATTR(i2c_mux_select, S_IRUGO | S_IWUSR, show_i2c_mux_select, store_i2c_mux_select);

static struct attribute *i2c_mux_attrs[] = {
	&dev_attr_i2c_mux_select.attr,
	NULL,
};

static struct attribute_group i2c_mux_attr_group = {
	.attrs  = i2c_mux_attrs,
};
static int fpga_i2c_mux_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = to_i2c_adapter(client->dev.parent);
	struct fpga_i2c_mux_plat_data *pdata = dev_get_platdata(&client->dev);
	struct i2c_mux_core *muxc;
	struct fpga_i2c_mux_data *data;
	int num, force;
	int err;

	muxc = i2c_mux_alloc(adap, &client->dev, NUM_I2C_MUX_CHANS,
			     sizeof(*data), 0, fpga_i2c_mux_select_chan,
			     fpga_i2c_mux_deselect);
	if (!muxc)
		return -ENOMEM;

	data = i2c_mux_priv(muxc);
	data->client = client;
	data->mux_reg_addr = ioremap(pdata->mux_reg_addr, 4);
	if (!data->mux_reg_addr) {
		dev_err(&client->dev, "fpga_i2c_mux : unable to map registers");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, muxc);

	/* Create an adapter for each channel. */
	for (num = 0; num < NUM_I2C_MUX_CHANS; num++) {
		force = I2C_MUX_CHAN_BASE_ID + (pdata->dev_id * 4);

		err = i2c_mux_add_adapter(muxc, 0, num, 0);
		if (err)
			goto virt_reg_failed;
	}

	dev_set_drvdata(&client->dev, data);

	err = sysfs_create_group(&client->dev.kobj, &i2c_mux_attr_group);
	if (err) {
		sysfs_remove_group(&client->dev.kobj, &i2c_mux_attr_group);
		dev_err(&client->dev, "Failed to create attr group\n");
		goto virt_reg_failed;
	}

	return 0;

virt_reg_failed:
	i2c_mux_del_adapters(muxc);
	iounmap(data->mux_reg_addr);
	return err;
}

static int fpga_i2c_mux_remove(struct i2c_client *client)
{
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);
	struct fpga_i2c_mux_data *data = i2c_mux_priv(muxc);

	iounmap(data->mux_reg_addr);
	i2c_mux_del_adapters(muxc);
	return 0;
}

static const struct i2c_device_id fpga_i2c_mux_id[] = {
	{ "pen_fpga_i2c_mux", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fpga_i2c_mux_id);

static struct i2c_driver fpga_i2c_mux_driver = {
	.driver = {
		.name = "pen_fpga_i2c_mux",
	},
	.probe		= fpga_i2c_mux_probe,
	.remove		= fpga_i2c_mux_remove,
	.id_table	= fpga_i2c_mux_id,
};

module_i2c_driver(fpga_i2c_mux_driver);

MODULE_DESCRIPTION("Pensando Systems FPGA I2C mux driver");
MODULE_AUTHOR("Ganesan Ramalingam <ganesanr@pensando.io>");
MODULE_LICENSE("GPL");

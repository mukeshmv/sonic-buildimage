#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/irqdomain.h>
#include "fpga_func1.h"


struct fpga_f1_data {
	void __iomem *membase;
	struct pci_dev *pdev;
};

static ssize_t show_td3_pwr_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fpga_f1_data *fdata = dev_get_drvdata(dev);

	return sprintf(buf, "%02X\n", ioread32(fdata->membase + TD3_PWR_STAT_REG) & 0x7);
}

static ssize_t show_elba1_pwr_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fpga_f1_data *fdata = dev_get_drvdata(dev);

	return sprintf(buf, "%02X\n", ioread32(fdata->membase + ELBA1_PWR_STAT_REG) & 0x7);
}

static ssize_t show_elba0_pwr_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fpga_f1_data *fdata = dev_get_drvdata(dev);

	return sprintf(buf, "%02X\n", ioread32(fdata->membase + ELBA0_PWR_STAT_REG) & 0x7);
}

static ssize_t store_td3_pwr_ctrl(struct device *dev,
				struct device_attribute *devattr,
				const char *buf,
				size_t len)
{
	struct fpga_f1_data *fdata = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val == 1) {
		/* set register to power on */
		iowrite32(PWR_ON, fdata->membase + TD3_PWR_CTRL_REG);
		udelay(1000);
		iowrite32(0x1fd, fdata->membase + TD_CTRL_REG);
		udelay(1000);
		iowrite32(0x0, fdata->membase + TD_CTRL_REG);
	} else if (val == 0) {
		/* set register to power off */
		iowrite32(0x1ff, fdata->membase + TD_CTRL_REG);
		iowrite32(PWR_OFF, fdata->membase + TD3_PWR_CTRL_REG);
	} else {
		return -EINVAL;
	}

	return len;
}

static ssize_t store_elba1_pwr_ctrl(struct device *dev,
				struct device_attribute *devattr,
				const char *buf,
				size_t len)
{
	struct fpga_f1_data *fdata = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val == 1) {
		/* set register to power on */
		iowrite32(0x1, fdata->membase + ELBA_1_CTRL_REG);
		iowrite32(PWR_ON, fdata->membase + ELBA1_PWR_CTRL_REG);
		udelay(3000);
		iowrite32(0x0, fdata->membase + ELBA_1_CTRL_REG);
		dev_info(&fdata->pdev->dev, "Turning on elba1 \n", buf);
	} else if (val == 0) {
		/* set register to power off */
		iowrite32(PWR_OFF, fdata->membase + ELBA1_PWR_CTRL_REG);
		iowrite32(0x3, fdata->membase + ELBA_1_CTRL_REG);
	} else {
		return -EINVAL;
	}

	return len;
}

static ssize_t store_elba0_pwr_ctrl(struct device *dev,
				struct device_attribute *devattr,
				const char *buf,
				size_t len)
{
	struct fpga_f1_data *fdata = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val == 1) {
		/* set register to power on */
		iowrite32(0x1, fdata->membase + ELBA_0_CTRL_REG);
		iowrite32(PWR_ON, fdata->membase + ELBA0_PWR_CTRL_REG);
		udelay(3000);
		iowrite32(0x0, fdata->membase + ELBA_0_CTRL_REG);
		dev_info(&fdata->pdev->dev, "Turning on elba0 \n", buf);
	} else if (val == 0) {
		/* set register to power off */
		iowrite32(PWR_OFF, fdata->membase + ELBA0_PWR_CTRL_REG);
		iowrite32(0x3, fdata->membase + ELBA_0_CTRL_REG);
	} else {
		return -EINVAL;
	}

	return len;
}

static DEVICE_ATTR(elba0_pwr_ctrl, S_IWUSR, NULL, store_elba0_pwr_ctrl);
static DEVICE_ATTR(elba1_pwr_ctrl, S_IWUSR, NULL, store_elba1_pwr_ctrl);
static DEVICE_ATTR(td3_pwr_ctrl, S_IWUSR, NULL, store_td3_pwr_ctrl);
static DEVICE_ATTR(elba0_pwr_status, S_IRUGO, show_elba0_pwr_status, NULL);
static DEVICE_ATTR(elba1_pwr_status, S_IRUGO, show_elba1_pwr_status, NULL);
static DEVICE_ATTR(td3_pwr_status, S_IRUGO, show_td3_pwr_status, NULL);

static struct attribute *fpga_attrs[] = {
	&dev_attr_elba0_pwr_ctrl.attr,
	&dev_attr_elba1_pwr_ctrl.attr,
	&dev_attr_td3_pwr_ctrl.attr,
	&dev_attr_elba0_pwr_status.attr,
	&dev_attr_elba1_pwr_status.attr,
	&dev_attr_td3_pwr_status.attr,
	NULL,
};

static struct attribute_group fpga_attr_group = {
	.attrs  = fpga_attrs,
};

static int fpga_f1_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)	
{
	int err;
	struct fpga_f1_data *fpga_data;
	uint32_t fpga_id;
	uint32_t elba_cntl;

	//dev_dbg(dev, "FPGA function 1 probe called\n");
	dev_info(&pdev->dev, "%s \n", __func__);

	fpga_data = devm_kzalloc(&pdev->dev, sizeof(*fpga_data), GFP_KERNEL);
	if (!fpga_data)
		return -ENOMEM;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable device %d\n", err);
		return err;
	}

	err = pcim_iomap_regions(pdev, 1 << 0, "fpga_f1");
	if (err) {
		dev_err(&pdev->dev, "Failed to iomap regions %d\n", err);
		goto err_disable;
	}

	fpga_data->membase = pcim_iomap_table(pdev)[0];
	if (IS_ERR(fpga_data->membase)) {
		dev_err(&pdev->dev, "pci_ioremap_bar() failed\n");
		err = -ENOMEM;
		goto err_release;
	}

	fpga_data->pdev = pdev;
	pci_set_drvdata(pdev, fpga_data);

	fpga_id = ioread32(fpga_data->membase + FPGA_REV_ID_REG) >> 16;
	dev_info(&pdev->dev, "FPGA ID 0x%x\n", fpga_id);

	err = sysfs_create_group(&pdev->dev.kobj, &fpga_attr_group);
	if (err) {
		sysfs_remove_group(&pdev->dev.kobj, &fpga_attr_group);
		dev_err(&pdev->dev, "Failed to create attr group\n");
		goto err_unmap;
	}

	return 0;

err_unmap:
	pci_iounmap(pdev, fpga_data->membase);
err_release:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);

	return err;
}

static void fpga_f1_remove(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "%s \n", __func__);
}

static const struct pci_device_id fpga_f1_id [] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PENSANDO, PCI_DEVICE_ID_PENSANDO_FPGA_F1) },
	{ }
};

static struct pci_driver fpga_f1_driver = {
    .name        = "fpga_f1",
    .id_table    = fpga_f1_id,
    .probe       = fpga_f1_probe,
    .remove      = fpga_f1_remove,
};

module_pci_driver(fpga_f1_driver);

MODULE_DESCRIPTION("Pensando Systems FPGA Power controller driver");
MODULE_AUTHOR("Ganesan Ramalingam <ganesanr@pensando.io>");
MODULE_LICENSE("GPL");

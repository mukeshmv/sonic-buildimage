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


struct fpga_f2_data {
	void __iomem *membase;
	struct pci_dev *pdev;
};

static ssize_t store_elba1_pwr_ctrl(struct device *dev,
				struct device_attribute *devattr,
				const char *buf,
				size_t len)
{
	struct fpga_f2_data *fdata = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	dev_info(&fdata->pdev->dev, "%s \n", buf);
	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val == 1) {
		/* set register to power on */
		iowrite32(0x1, fdata->membase + SPI_FLSH5_MUXSEL_REG);
		iowrite32(0x1, fdata->membase + SPI_FLSH2_MUXSEL_REG);
		iowrite32(0x1, fdata->membase + SPI_FLSH7_MUXSEL_REG);
	} else if (val == 0) {
		/* set register to power off */
		iowrite32(0x0, fdata->membase + SPI_FLSH5_MUXSEL_REG);
		iowrite32(0x0, fdata->membase + SPI_FLSH2_MUXSEL_REG);
		iowrite32(0x0, fdata->membase + SPI_FLSH7_MUXSEL_REG);
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
	struct fpga_f2_data *fdata = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val == 1) {
		/* set register to power on */
		iowrite32(0x1, fdata->membase + SPI_FLSH4_MUXSEL_REG);
		iowrite32(0x1, fdata->membase + SPI_FLSH1_MUXSEL_REG);
		iowrite32(0x1, fdata->membase + SPI_FLSH6_MUXSEL_REG);
	} else if (val == 0) {
		/* set register to power off */
		iowrite32(0x0, fdata->membase + SPI_FLSH4_MUXSEL_REG);
		iowrite32(0x0, fdata->membase + SPI_FLSH1_MUXSEL_REG);
		iowrite32(0x0, fdata->membase + SPI_FLSH6_MUXSEL_REG);
	} else {
		return -EINVAL;
	}

	return len;
}

static DEVICE_ATTR(elba0_pwr_ctrl, S_IWUSR, NULL, store_elba0_pwr_ctrl);
static DEVICE_ATTR(elba1_pwr_ctrl, S_IWUSR, NULL, store_elba1_pwr_ctrl);

static struct attribute *fpga_attrs[] = {
	&dev_attr_elba0_pwr_ctrl.attr,
	&dev_attr_elba1_pwr_ctrl.attr,
	NULL,
};

static struct attribute_group fpga_attr_group = {
	.attrs  = fpga_attrs,
};

static int fpga_f2_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)	
{
	int err;
	struct fpga_f2_data *fpga_data;
	uint32_t fpga_id;
	uint32_t elba_cntl;

	//dev_dbg(dev, "FPGA function 2 probe called\n");
	dev_info(&pdev->dev, "%s \n", __func__);

	fpga_data = devm_kzalloc(&pdev->dev, sizeof(*fpga_data), GFP_KERNEL);
	if (!fpga_data)
		return -ENOMEM;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable device %d\n", err);
		return err;
	}

	err = pcim_iomap_regions(pdev, 1 << 0, "fpga_f2");
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

static void fpga_f2_remove(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "%s \n", __func__);
}

static const struct pci_device_id fpga_f2_id [] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PENSANDO, PCI_DEVICE_ID_PENSANDO_FPGA_F2) },
	{ }
};

static struct pci_driver fpga_f2_driver = {
    .name        = "fpga_f2",
    .id_table    = fpga_f2_id,
    .probe       = fpga_f2_probe,
    .remove      = fpga_f2_remove,
};

module_pci_driver(fpga_f2_driver);

MODULE_DESCRIPTION("Pensando Systems FPGA SPI controller driver");
MODULE_AUTHOR("Ganesan Ramalingam <ganesanr@pensando.io>");
MODULE_LICENSE("GPL");

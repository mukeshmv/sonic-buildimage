#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/irqdomain.h>
#include <linux/i2c.h>
#include <linux/platform_data/i2c-ocores.h>
#include "fpga_func1.h"
#include "fpga_i2c_mux.h"

#define I2C_BASE_OFFSET 0x1000
#define I2C_NEXT_CONTROLER_OFFSET 0x20
#define REG_LENGTH	4 /* 4 bytes */
#define NUMBER_OF_I2C_CONTROLLER 17

struct fpga_i2c_priv_data {
	void __iomem *membase;
	struct pci_dev *pdev;
};


/* ------------------------------------------------------------------*/

static struct resource i2c_resources2[] = {
      [0] = {
              .start  = 0,
              .end    = 0,
              .flags  = IORESOURCE_MEM,
      },
};

/* Data will be filled later */
static struct fpga_i2c_mux_plat_data i2c_mux_plat_data2 = {
	.mux_reg_addr   = 0,
	.dev_id		= 2,
};

struct i2c_board_info  board_info2 [] = {
	{
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
		.platform_data = &i2c_mux_plat_data2,
	}
}; 

static struct ocores_i2c_platform_data i2c_data2 = {
      .reg_io_width   = 4,            /* Four bytes between registers */
      .clock_khz      = 62500,        /* input clock of 124KHz */
      .devices        = board_info2, 
      .num_devices    = ARRAY_SIZE(board_info2), 
};

static struct platform_device i2c_dev2 = {
      .name                   = "ocores-i2c",
      .id		      = 2,
      .dev = {
              .platform_data  = &i2c_data2,
      },
      .num_resources          = 1,
      .resource               = i2c_resources2,
};
static struct resource i2c_resources1[] = {
      [0] = {
              .start  = 0,
              .end    = 0,
              .flags  = IORESOURCE_MEM,
      },
};

/* Data will be filled later */
static struct fpga_i2c_mux_plat_data i2c_mux_plat_data1 = {
	.mux_reg_addr	= 0,
	.dev_id		= 1,
};

struct i2c_board_info  board_info1 [] = {
	{
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x02),
		.platform_data = &i2c_mux_plat_data1,
	}
}; 

static struct ocores_i2c_platform_data i2c_data1 = {
      .reg_io_width   = 4,            /* Four bytes between registers */
      .clock_khz      = 62500,        /* input clock of 124KHz */
      .devices        = board_info1, 
      .num_devices    = ARRAY_SIZE(board_info1), 
};

static struct platform_device i2c_dev1 = {
      .name                   = "ocores-i2c",
      .id		      = 1,
      .dev = {
              .platform_data  = &i2c_data1,
      },
      .num_resources          = 1,
      .resource               = i2c_resources1,
};
static struct resource i2c_resources0[] = {
      [0] = {
              .start  = 0,
              .end    = 0,
              .flags  = IORESOURCE_MEM,
      },
};

/* Data will be filled later */
static struct fpga_i2c_mux_plat_data i2c_mux_plat_data0 = {
	.mux_reg_addr   = 0,
	.dev_id		= 0,
};

struct i2c_board_info  board_info0 [] = {
	{
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x01),
		.platform_data = &i2c_mux_plat_data0,
	}
}; 

static struct ocores_i2c_platform_data i2c_data0 = {
      .reg_io_width   = 4,            /* Four bytes between registers */
      .clock_khz      = 62500,        /* input clock of 124KHz */
      .devices        = board_info0, 
      .num_devices    = ARRAY_SIZE(board_info0), 
};

static struct platform_device i2c_dev0 = {
      .name                   = "ocores-i2c",
      .id		      = 0,
      .dev = {
              .platform_data  = &i2c_data0,
      },
      .num_resources          = 1,
      .resource               = i2c_resources0,
};


/* ------------------------------------------------------------------*/

//static struct platform_device *i2c_dev[NUMBER_OF_I2C_CONTROLLER];
static int fpga_i2c_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)	
{
	int err;
	struct fpga_i2c_priv_data *fpga_data;
	uint32_t fpga_id;
	//struct ocores_i2c_platform_data *platform_data;

	//dev_dbg(dev, "FPGA function 3 probe called\n");
	dev_info(&pdev->dev, "%s \n", __func__);

	fpga_data = devm_kzalloc(&pdev->dev, sizeof(*fpga_data), GFP_KERNEL);
	if (!fpga_data)
		return -ENOMEM;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable device %d\n", err);
		return err;
	}
#if 0
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
#endif	

	i2c_dev0.resource[0].start = pdev->resource[0].start + I2C_BASE_OFFSET +
			(I2C_NEXT_CONTROLER_OFFSET * 0);
	i2c_dev0.resource[0].end = i2c_dev0.resource[0].start + 0x14;

	i2c_dev1.resource[0].start = pdev->resource[0].start + I2C_BASE_OFFSET +
			(I2C_NEXT_CONTROLER_OFFSET * 1);
	i2c_dev1.resource[0].end = i2c_dev1.resource[0].start + 0x14;

	i2c_dev2.resource[0].start = pdev->resource[0].start + I2C_BASE_OFFSET +
			(I2C_NEXT_CONTROLER_OFFSET * 2);
	i2c_dev2.resource[0].end = i2c_dev2.resource[0].start + 0x14;

	void __iomem *i2c0 = ioremap(i2c_dev0.resource[0].start, 0x14);

	uint32_t prscl_lo = ioread32(i2c0); 
	uint32_t prscl_hi = ioread32(i2c0 + 0x4); 
	dev_info(&pdev->dev, "i2c0 prscl hi : 0x%x lo : 0x%x \n", prscl_hi, prscl_lo);
	iounmap(i2c0);


	void __iomem *i2c1 = ioremap(i2c_dev1.resource[0].start, 0x14);

	prscl_lo = ioread32(i2c1); 
	prscl_hi = ioread32(i2c1 + 0x4); 
	dev_info(&pdev->dev, "i2c1 prscl hi : 0x%x lo : 0x%x \n", prscl_hi, prscl_lo);
	iounmap(i2c1);



	i2c_mux_plat_data0.mux_reg_addr = pdev->resource[0].start + I2C_BASE_OFFSET + (I2C_NEXT_CONTROLER_OFFSET * 0) + 0x14; 
	i2c_mux_plat_data0.dev_id = 0;
	i2c_mux_plat_data1.mux_reg_addr = pdev->resource[0].start + I2C_BASE_OFFSET + (I2C_NEXT_CONTROLER_OFFSET * 1) + 0x14;
	i2c_mux_plat_data1.dev_id = 1;
	i2c_mux_plat_data2.mux_reg_addr = pdev->resource[0].start + I2C_BASE_OFFSET + (I2C_NEXT_CONTROLER_OFFSET * 2) + 0x14; 
	i2c_mux_plat_data2.dev_id = 2;

	platform_device_register(&i2c_dev0);
	platform_device_register(&i2c_dev1);
	platform_device_register(&i2c_dev2);

	
#if 0 
	for(i = 0; i < 3; i++) {
		i2c_dev[i] = platform_device("ocores-i2c", i);
		i2c_dev[i]->num_resources = 1;
		i2c_dev[i]->resource = kzalloc(sizeof(struct resource), GFP_KERNEL);
		if (!i2c_dev[i]->resource)
			return -ENOMEM;
			
		i2c_dev[i]->resource[0].start = pdev->resource[0] + I2C_BASE_OFFSET + 
			(I2C_NEXT_CONTROLER_OFFSET * i);
		i2c_dev[i]->resource[0].end = i2c_dev[i].resource[0].start + 0x14;
		i2c_dev[i]->dev.platform_data = 
			kzalloc(sizeof(struct ocores_i2c_platform_data), GFP_KERNEL);
		if (!i2c_dev[i]->dev.platform_data)
			return -ENOMEM;

		platform_data = i2c_dev[i]->dev.platform_data;
		platform_data->reg_io_width = 4;
		// TODO : Confirm clock speed with hardware team
		platform_data->ip_clock_khz = 100; 


		i2c_dev[i]->dev.platform_data->devices = 
			kzalloc(sizeof(struct i2c_board_info), GFP_KERNEL);
		strscpy(i2c_dev[i]->dev.platform_data->devices->type, "fpga_i2c_mux", I2C_NAME_SIZE);
		i2c_dev[i]->dev.platform_data->devices->addr = 0x41;
	}
#endif
	return 0;
err_unmap:
	pci_iounmap(pdev, fpga_data->membase);
err_release:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);

	return err;
}

static void fpga_i2c_remove(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "%s \n", __func__);
}

static const struct pci_device_id fpga_i2c_id [] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PENSANDO, PCI_DEVICE_ID_PENSANDO_FPGA_F3) },
	{ }
};

static struct pci_driver fpga_i2c_driver = {
	.name 		= "fpga-i2c",
	.id_table 	= fpga_i2c_id,
	.probe 		= fpga_i2c_probe,
	.remove		= fpga_i2c_remove,
};

module_pci_driver(fpga_i2c_driver);

MODULE_DESCRIPTION("Pensando Systems FPGA I2C controller driver");
MODULE_AUTHOR("Ganesan Ramalingam <ganesanr@pensando.io>");
MODULE_LICENSE("GPL");

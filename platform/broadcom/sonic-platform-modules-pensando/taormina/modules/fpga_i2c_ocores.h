/**
 * (c) Copyright 2019-2021 Hewlett Packard Enterprise Development LP
 *
 * Confidential computer software. Valid license from Hewlett Packard
 * Enterprise required for possession, use or copying.
 *
 * Consistent with FAR 12.211 and 12.212, Commercial Computer Software,
 * Computer Software Documentation, and Technical Data for Commercial Items
 * are licensed to the U.S. Government under vendor's standard commercial
 * license.
 */

/*
 * hpe-fpga-i2c-ocores.h
 * This file declares the macros like error code specific
 * to hpe-fpga-i2c-ocore driver.
 */

#ifndef HPE_FPGA_I2C_OCORES_H
#define HPE_FPGA_I2C_OCORES_H

/*
 * HPE I2C driver unique error codes
 *  EI2C_RXACK:       Received NACK from slave.Slave not present/invalid/not
 *                    responding
 *  EI2C_BUSY:        i2c bus is busy and is being used by different master.wait
 *                    and retry
 *  EI2C_ARBLOST:     arbitration lost. retry the txn
 *  EI2C_TIP:         i2c command (read/write/start/stop/iack) is in progress.
 *                    Slave not responding to commands or slave not present.
 *                    check state of the system/card/device before retry.
 */
#define EI2C_RXACK 1000
#define EI2C_BUSY 1001
#define EI2C_ARBLOST 1002
#define EI2C_TIP 1003

// For HPE, Vendor id is defined in linux kernel tree.
// Pensando doesn't have an entry yet in kernel.
#define PCI_VENDOR_ID_PENSANDO 0x1dd8
#define PCI_DEVICE_ID_PENSANDO_FPGA_I2C 0x0006

#define OCI2C_BAR_MASK 1
#define OCI2C_FUNC_CAP_ADDR 96
#define OCI2C_BASE_ADDR 4096
#define OCI2C_REG_IO_WIDTH 4  // memory-mapped registers are 4 byte aligned

/* MSI Interrupt Registers */
#define OCI2C_MSI_PEND 768
#define OCI2C_MSI0_INT_EN 772

#endif /* HPE_FPGA_I2C_OCORES_H */



/* AT24C256 EEPROM */
#define AT24C256 "24c256"
#define AT24C256_MAX_BYTES 32768
#define AT24C256_PAGE_SIZE 64

/*
      * I2C clock frequency is determined by prescale setting.
      * The required prescale value is provided by HW team (FPGA register document)
      */
#define I2C_CLOCK_PRESCALE_100KHZ 124 /* TODO: Verify with HW team */
#define I2C_CLOCK_PRESCALE_400KHZ 30  /* TODO: Verify with HW team */

/* Add all I2C custom devices for the given FPGA and controller */
void hpe_fpga_i2c_add_devices(struct i2c_adapter *adap, int fpga_id,
    	                                int controller_num, u16 **addr_table,
    	                                int *addr_table_len);

/* Return the number of controllers on the given FPGA */
int hpe_fpga_i2c_num_controllers(void __iomem *base_addr, int fpga_id);

int hpe_fpga_i2c_prescale(int fpga_id, int controller_num);

/*
      * Some FPGAs have holes in the controller number space.  Return 1 if this
      * controller number is valid for this FPGA or 0 if not.
      */
int hpe_fpga_i2c_valid_controller(int fpga_id, uint controller_num);

int hpe_fpga_i2c_prescale(int fpga_id, int controller_num);

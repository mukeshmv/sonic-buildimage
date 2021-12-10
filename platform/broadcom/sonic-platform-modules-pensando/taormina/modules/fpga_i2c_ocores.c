// (c) Copyright 2016-2021 Hewlett Packard Enterprise Development LP
// All Rights Reserved.
//
// The contents of this software are proprietary and confidential
// to the Hewlett Packard Enterprise Development LP.  No part of this
// program may be photocopied, reproduced, or translated into another
// programming language without prior written consent of the
// Hewlett Packard Enterprise Development LP.
//
// Linux driver for HPE FPGAs that use the OpenCores I2C module
//
// This driver was rewritten by HPE for ArubaOS and the Ridley program.  It was
// patterned after the i2c-ocores I2C bus driver for OpenCores I2C controller
// by Peter Korsgaard <jacmet@sunsite.dk>.  That driver contained the following
// notice.
//
//  This file is licensed under the terms of the GNU General Public License
//  version 2.  This program is licensed "as is" without any warranty of any
//  kind, whether express or implied.

#define DEBUG  // undefine to remove dynamically enabled debug code entirely

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/wait.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include "fpga_i2c_ocores.h"
#include "fpga_i2c_mux.h"

// Configurable module parameters for debugging

// Dynamically enabled debug tracing.
//
// Enable by writing to the /sys/module file.  For example,
//
// echo 2 > /sys/module/hpe_fpga_i2c_ocores/parameters/debug_level
//
enum debug_level {
    DEBUG_NONE,   // 0 = no debug tracing
    DEBUG_ERR,    // 1 = trace I2C errors
    DEBUG_INFO,   // 2 = trace useful information
    DEBUG_MSG,    // 3 = also trace summary of each I2C message
    DEBUG_CMD,    // 4 = also trace I2C controller commands
    DEBUG_READ,   // 5 = also trace data read
    DEBUG_TIP,    // 6 = also trace TIP polling
    DEBUG_STATE,  // 7 = also trace state machine
    DEBUG_MAX
};
static int debug_level = 1;
#ifdef DEBUG
module_param(debug_level, int, (S_IRUGO | S_IWUSR));
MODULE_PARM_DESC(debug_level, "Debug tracing level");
#endif

// Limit debug tracing to a specific I2C bus.  The bus number corresponds to
// the X in the /dev/i2c-X file for the controller.
//
// Enable by writing to the /sys/module file.  For example, to trace only
// /dev/i2c-30 enter
//
// echo 30 > /sys/module/hpe_fpga_i2c_ocores/parameters/debug_bus
//
static int debug_bus;
#ifdef DEBUG
module_param(debug_bus, int, (S_IRUGO | S_IWUSR));
MODULE_PARM_DESC(debug_bus, "Only trace transactions for the given bus number");
#endif

// Limit debug tracing to a specific I2C slave address.
//
// Enable by writing to the /sys/module file.  For example,
//
// echo 0x50 > /sys/module/hpe_fpga_i2c_ocores/parameters/debug_slave
//
static int debug_slave;
#ifdef DEBUG
module_param(debug_slave, int, (S_IRUGO | S_IWUSR));
MODULE_PARM_DESC(debug_slave,
                 "Only trace transactions for the given slave address");
#endif

// Macros that check the trace filters before emitting a message
#define DO_DEBUG(level)                               \
    (debug_level >= (level) &&                        \
     (debug_bus == 0 || debug_bus == i2c->adap.nr) && \
     (debug_slave == 0 || debug_slave == i2c->cur_slave))
#define PR_DEBUG(level, fmt, ...)                                  \
    do                                                             \
        if (DO_DEBUG(level)) pr_debug("i2c: " fmt, ##__VA_ARGS__); \
    while (0)

// Dynamically adjustable pad time between I2C bus commands.  This only applies
// to polling mode.
//
// Enable by writing to the /sys/module file.  For example,
//
// echo 100 > /sys/module/hpe_fpga_i2c_ocores/parameters/pad_time
//
static int pad_time;
module_param(pad_time, int, (S_IRUGO | S_IWUSR));
MODULE_PARM_DESC(pad_time, "Pad time between I2C bus commands in microseconds");

#define OCI2C_LOG_MSG_SIZE 40

// OpenCore I2C registers
#define OCI2C_PRELOW 0
#define OCI2C_PREHIGH 1
#define OCI2C_CONTROL 2
#define OCI2C_DATA 3
#define OCI2C_CMD 4     // write only, same offset as STATUS reg
#define OCI2C_STATUS 4  // read only, same offset as CMD reg

// HPE FPGA extension registers
#define OCI2C_MUX_SELECT 5
#define OCI2C_RESET 6  // write 0xD to reset the I2C controller
#define OCI2C_SEMAPHORE 7

// The total number of registers
#define OCI2C_REG_CNT 8

// Control register bits
#define OCI2C_CTRL_EN 0x80   // enable
#define OCI2C_CTRL_IEN 0x40  // enable interrupts

// Command register bits
#define OCI2C_CMD_START_BIT 0x80  // generate start condition
#define OCI2C_CMD_STOP_BIT 0x40   // generate stop condition
#define OCI2C_CMD_READ_BIT 0x20   // read from slave
#define OCI2C_CMD_WRITE_BIT 0x10  // write to slave
#define OCI2C_CMD_NOACK_BIT 0x08  // do not acknowledge read
#define OCI2C_CMD_IACK_BIT 0x01   // clear pending interrupt

// Status register bits
#define OCI2C_STAT_NOACK 0x80    // no acknowledge
#define OCI2C_STAT_BUSY 0x40     // bus busy after START
#define OCI2C_STAT_ARBLOST 0x20  // arbitration lost
#define OCI2C_STAT_TIP 0x02      // transfer in progress
#define OCI2C_STAT_INT 0x01      // interrupt pending
#define OCI2C_STAT_ERR 0xFF      // PCIE read error

// Combo commands
#define OCI2C_CMD_START \
    (OCI2C_CMD_START_BIT + OCI2C_CMD_WRITE_BIT + OCI2C_CMD_IACK_BIT)
#define OCI2C_CMD_STOP (OCI2C_CMD_STOP_BIT + OCI2C_CMD_IACK_BIT)
#define OCI2C_CMD_READ (OCI2C_CMD_READ_BIT + OCI2C_CMD_IACK_BIT)
#define OCI2C_CMD_READ_LAST \
    (OCI2C_CMD_READ_BIT + OCI2C_CMD_NOACK_BIT + OCI2C_CMD_IACK_BIT)
#define OCI2C_CMD_WRITE (OCI2C_CMD_WRITE_BIT + OCI2C_CMD_IACK_BIT)
#define OCI2C_CMD_IACK (OCI2C_CMD_IACK_BIT)

/*
    Command codes for special I2C operations:
    I2C_NORMAL: normal msg (read/ write)
    I2C_MUX_SET: special ops: set mux to specific value
    I2C_RST_CONTLR: special ops: reset the controller which communicates
                    to specific i2c device
    I2C_RST_SLAVE: special ops: reset the i2c device
    NOTE: Defines should match the ones defined in i2c.c (user space)
*/
#define I2C_NORMAL 0
#define I2C_MUX_SET 1
#define I2C_RST_CONTLR 2
#define I2C_RST_SLAVE 3

/* HPE I2C OCORES IP uses additional control registers
   to set mux, reset controller, set semaphore etc. Native i2c subsystem
   infrastructure in the kernel is not aware of these controls and hence
   provides no path from user space to kernel space to enable/disables the
   extra controls. To overcome the limitation, we use reserved/unused i2c
   7 bit slave address- 0x01 (called CBUS device address).Any commands to
   this address is considered as "HPE I2C special commands".The data buffer
   passed in indicates SPL_CMD and SPL_CMD_DATA.
   <msg->addr = HPE_OCORES_I2C_CBUS_ADDR
   <msg->buf[0]> = SPL_CMD (i2c_mux, i2c_rst_controllers, i2c_rst_slave)
   <msg->buf[1]> = SPL_CMD_DATA (mux_value, controller_nux, slave_addr)
*/
#define HPE_OCORES_I2C_CBUS_ADDR 0x01

// Transfer states
enum oci2c_state_e {
    STATE_PRE_START,  // ready to start a new message
    STATE_START,      // ready to send a START command
    STATE_DATA,       // ready to transfer data
    STATE_READ,       // READ command in progress
    STATE_WRITE,      // WRITE command in progress
    STATE_STOP,       // ready to send STOP
    STATE_DONE,       // message sequence complete
    STATE_EXIT        // same as DONE, but without IACK
};

// Data structure for each I2C controller
struct oci2c_controller {
    int             fpga_id;            // FPGA ID this controller resides on
    int             controller_num;     // controller index in FPGA
    bool            use_isr;            // use interrupts for this controller
    struct pci_dev *pdev;               // PCI device for this contoller
    void __iomem *     base;            // memory-mapped address of controller
    wait_queue_head_t  wait;            // wait structure
    struct i2c_adapter adap;            // I2C adapter
    u16 *              addr_table;      // bus address mapping table
    int                addr_table_len;  // size of bus address mapping table
    struct i2c_msg *   msg;             // current message list for controller
    int                num_msg;         // length of message list
    int                cur_msg;         // current message in list
    int                cur_slave;       // current slave address
    int                cur_byte;        // current byte in message data
    int                errno;           // error code to return
    enum oci2c_state_e state;           // current state
    struct mutex       mutex;           // mutex used to remove controller
};

// Data structure for PCI bus device, which is an HPE FPGA containing one or
// more OpenCores I2C controllers.
struct oci2c_pci {
    struct pci_dev *pdev;                // PCI device structure
    void __iomem *           base;       // memory-mapped address of the device
    int                      fpga_id;    // FPGA ID of the device
    int                      i2c_count;  // number of controllers on the device
    bool                     use_isr;    // use interrupts for the device
    struct oci2c_controller *i2c;  // array of I2C controllers on the device
};

// Translate state name to string for tracing
static const char *
oci2c_state_name(enum oci2c_state_e state) {
    switch (state) {
        case STATE_PRE_START:
            return "PRE_START";
        case STATE_START:
            return "START";
        case STATE_DATA:
            return "DATA";
        case STATE_READ:
            return "READ";
        case STATE_WRITE:
            return "WRITE";
        case STATE_STOP:
            return "STOP";
        case STATE_DONE:
            return "DONE";
        case STATE_EXIT:
            return "EXIT";
        default:
            return "???";
    }
}

// Translate command to string for tracing
static char *
oci2c_cmd_name(int cmd, char *str, int size) {
    if (size > 0) str[0] = '\0';
    if (cmd & OCI2C_CMD_START_BIT) {
        strncat(str, "START ", size - strlen(str) - 1);
    }
    if (cmd & OCI2C_CMD_STOP_BIT) {
        strncat(str, "STOP ", size - strlen(str) - 1);
    }
    if (cmd & OCI2C_CMD_READ_BIT) {
        strncat(str, "READ ", size - strlen(str) - 1);
    }
    if (cmd & OCI2C_CMD_WRITE_BIT) {
        strncat(str, "WRITE ", size - strlen(str) - 1);
    }
    if (cmd & OCI2C_CMD_NOACK_BIT) {
        strncat(str, "NOACK ", size - strlen(str) - 1);
    }
    if (cmd & OCI2C_CMD_IACK_BIT) {
        strncat(str, "IACK", size - strlen(str) - 1);
    }
    return str;
}

// Format a data buffer in hex for tracing
static char *
oci2c_trace_data(const char *buf, int len, char *str, int size) {
    char *start        = str;
    if (size > 0) *str = '\0';
    while (len > 0 && size >= 2) {
        int n = snprintf(str, size, "%02X ", (unsigned char)*buf);
        buf += 1;
        len -= 1;
        str += n;
        size -= n;
    }
    return start;
}

// Generate a debug trace for the given message.  Note that we assume the
// caller has already checked the trace filters so we don't need to check them
// again.
static void
oci2c_trace_msg(const struct i2c_msg *msg, int msg_num, bool read_data_valid) {
    const int trace_limit = 4;  // only trace up to this many bytes of data
    bool      trace_data  = ((msg->flags & I2C_M_RD) == 0 || read_data_valid);
    char      str[trace_limit * 3];

    pr_debug("i2c:   ----- msg %d: addr 0x%02X %s %d bytes%s : %s%s\n", msg_num,
             msg->addr, (msg->flags & I2C_M_RD ? "READ " : "WRITE"), msg->len,
             (msg->flags & I2C_M_NOSTART ? " with NOSTART" : ""),
             (trace_data ? oci2c_trace_data(msg->buf, msg->len, str, sizeof str)
                         : ""),
             (trace_data && msg->len > trace_limit ? " ..." : ""));
}

// Write to an I2C controller register
static inline void
oci2c_setreg(const struct oci2c_controller *i2c, int reg, u8 value) {
    iowrite32(value, i2c->base + (reg * OCI2C_REG_IO_WIDTH));
}

// Read from an I2C controller register
static inline u8
oci2c_getreg(const struct oci2c_controller *i2c, int reg) {
    return ioread32(i2c->base + (reg * OCI2C_REG_IO_WIDTH));
}

// Check if the I2C bus is busy
static bool
oci2c_is_bus_busy(const struct oci2c_controller *i2c) {
    u8 stat = oci2c_getreg(i2c, OCI2C_STATUS);
    // PCIE read returns all 1s on error
    if (stat == OCI2C_STAT_ERR) {
        PR_DEBUG(DEBUG_ERR,
                 "  PCIE error. calling process=%s,"
                 " controller=%s, slave=0x%X\n",
                 current->comm, i2c->adap.name, i2c->cur_slave);
    }
    return ((stat & OCI2C_STAT_BUSY) != 0);
}

// Check if an I2C controller is enabled
static bool
oci2c_is_enabled(const struct oci2c_controller *i2c) {
    u8 ctrl = oci2c_getreg(i2c, OCI2C_CONTROL);
    return ((ctrl & OCI2C_CTRL_EN) != 0);
}

// Disable an I2C controller
static void
oci2c_disable(const struct oci2c_controller *i2c) {
    // Disable the controller
    u8 ctrl = oci2c_getreg(i2c, OCI2C_CONTROL);
    oci2c_setreg(i2c, OCI2C_CONTROL,
                 (ctrl & ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN)));
}

// Enable an I2C controller.  Do not call from ISR context!
static int
oci2c_enable(const struct oci2c_controller *i2c) {
    u8  ctrl = oci2c_getreg(i2c, OCI2C_CONTROL);
    int prescale;

    // Make sure the controller is disabled while we configure speed
    oci2c_setreg(i2c, OCI2C_CONTROL,
                 (ctrl & ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN)));

    // Configure the prescale value for the bus speed
    prescale = 124;
    oci2c_setreg(i2c, OCI2C_PRELOW, ((prescale >> 0) & 0xff));
    oci2c_setreg(i2c, OCI2C_PREHIGH, ((prescale >> 8) & 0xff));

    // Make sure the interrupt flag is clear and enable the controller
    oci2c_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
    oci2c_setreg(i2c, OCI2C_CONTROL,
                 (ctrl | OCI2C_CTRL_EN | (i2c->use_isr ? OCI2C_CTRL_IEN : 0)));

    // Give the controller a chance to initialize
    usleep_range(1000, 1500);

    return 0;
}

// Send an I2C command to a controller
static void
oci2c_cmd(const struct oci2c_controller *i2c, int cmd, u8 data) {
    char str[OCI2C_LOG_MSG_SIZE];
    PR_DEBUG(DEBUG_CMD, "  send command 0x%02X (%s) with data 0x%02X\n", cmd,
             oci2c_cmd_name(cmd, str, sizeof str), data);

    // Write the command to the controller
    oci2c_setreg(i2c, OCI2C_DATA, data);
    oci2c_setreg(i2c, OCI2C_CMD, cmd);
}

// Advance to the next message in the current transaction
static void
oci2c_next_msg(struct oci2c_controller *i2c) {
    // Reset the data pointer
    i2c->cur_byte = 0;

    // Increment the message pointer
    i2c->cur_msg++;

    // Determine the next state
    if (i2c->cur_msg < i2c->num_msg) {
        // More messages left - prepare to process the next one
        i2c->state = STATE_PRE_START;
    } else if (i2c->cur_slave) {
        // No messages left and we have a current address - send a STOP
        PR_DEBUG(DEBUG_CMD, "  ----- end of messages: send STOP\n");
        i2c->state = STATE_STOP;
    } else {
        // No messages left and we never sent a START - we are done
        PR_DEBUG(DEBUG_CMD, "  ----- end of messages: DONE\n");
        i2c->state = STATE_DONE;
    }
}

// Handle special a operation message that is not a normal data transfer.
// These operations are sent to a special address that is never used by real
// I2C devices.  The operation is in the first byte of the message and any
// parameter needed is in the second.
static void
oci2c_special_op(struct oci2c_controller *i2c) {
    struct i2c_msg *msg  = &i2c->msg[i2c->cur_msg];
    char            cmd  = msg->buf[0];
    u8              data = msg->buf[1];

    // Execute the special operation
    switch (cmd) {
        case I2C_MUX_SET:
            PR_DEBUG(DEBUG_MSG, "  ----- msg %d: MUX SELECT %d\n", i2c->cur_msg,
                     data);
            oci2c_setreg(i2c, OCI2C_MUX_SELECT, data);
            break;

        case I2C_RST_CONTLR:
            PR_DEBUG(DEBUG_MSG, "  ----- msg %d: RESET CONTROLLER\n",
                     i2c->cur_msg);

            // Set the reset register, wait a bit, then release it
            oci2c_setreg(i2c, OCI2C_RESET, 0xD);
            usleep_range(500, 750);
            oci2c_setreg(i2c, OCI2C_RESET, 0x0);
            break;

        case I2C_RST_SLAVE:
            PR_DEBUG(DEBUG_MSG, "  ----- msg %d: RESET SLAVE\n", i2c->cur_msg);

            // For reset slave, don't use the state machine for start and data
            // command as slave is not expected to behave nicely.  Instead, do
            // raw transactions here.  TODO - if specific slaves need a custom
            // reset sequence, add code here for them.
            oci2c_cmd(i2c, OCI2C_CMD_START, (data << 1));

            // Send the reset sequence of 9 clock cycles of 1.  Assuming the
            // slave is not responding, this works as follows.  We write 0xFF
            // as normal data, then when the slave would normally pull the data
            // line to 0 in acknowledgement, we expect the slave will leave it
            // high, resulting in the data line being high for 9 clocks.
            oci2c_cmd(i2c, OCI2C_CMD_WRITE, 0xFF);

            // Send STOP as part of this recovery
            oci2c_cmd(i2c, OCI2C_CMD_STOP, 0);
            break;

        default:
            PR_DEBUG(DEBUG_MSG, "  ----- msg %d: UNKNOWN COMMAND %d %d\n",
                     i2c->cur_msg, cmd, data);
            i2c->errno = EBADMSG;
            break;
    }
}

// Actions for state PRE_START - Handle special ops, if any, or else start a
// new message
static void
oci2c_pre_start(struct oci2c_controller *i2c) {
    struct i2c_msg *msg = &i2c->msg[i2c->cur_msg];

    if (msg->addr == HPE_OCORES_I2C_CBUS_ADDR) {
        // Execute special operations
        oci2c_special_op(i2c);

        // Exit state machine immediately without processing any remaining
        // messages or sending an IACK command to the controller.
        i2c->state = STATE_EXIT;
    } else {
        // We are about to start a new message.  If the device address for this
        // message is different from the previous message, we first send a STOP
        // to terminate the previous sequence.
        if (i2c->cur_slave && i2c->cur_slave != msg->addr)
            i2c->state = STATE_STOP;
        else
            i2c->state = STATE_START;
    }
}

// Actions for state START - ready to start a new message
static void
oci2c_start(struct oci2c_controller *i2c) {
    struct i2c_msg *msg      = &i2c->msg[i2c->cur_msg];
    int             old_addr = i2c->cur_slave;

    // Update the current slave address
    i2c->cur_slave = msg->addr;

    // Trace the beginning of a message.  However, in the special case where
    // the debug level is exactly MSG and this is a READ command, we trace at
    // the end so we can dump the data received.
    if (DO_DEBUG(DEBUG_MSG) &&
        !(debug_level == DEBUG_MSG && (msg->flags & I2C_M_RD)))
        oci2c_trace_msg(msg, i2c->cur_msg, false);

    // Send a START if this is the first message, or if this is a different
    // address from the previous message, or if the device expects repeated
    // START commands between messages.
    if (i2c->cur_msg == 0 || msg->addr != old_addr ||
        (msg->flags & I2C_M_NOSTART) == 0) {
        // If this is the first message, check for BUS_BUSY and exit state
        // machine if required.
        if (!i2c->cur_msg && oci2c_is_bus_busy(i2c)) {
            PR_DEBUG(DEBUG_ERR,
                     "  bus is busy. calling process=%s,"
                     " controller=%s, slave=0x%X\n",
                     current->comm, i2c->adap.name, i2c->cur_slave);
            i2c->errno = EBUSY;
            i2c->state = STATE_DONE;
        } else {
            u8 data = ((msg->addr << 1) | ((msg->flags & I2C_M_RD) ? 1 : 0));
            oci2c_cmd(i2c, OCI2C_CMD_START, data);
        }
    }

    // Advance to the next state only if there are no errors.
    if (!i2c->errno) {
        i2c->state = STATE_DATA;
    }
}

// Actions for state DATA - ready to transfer a data byte
static void
oci2c_data(struct oci2c_controller *i2c) {
    struct i2c_msg *msg  = &i2c->msg[i2c->cur_msg];
    u8              stat = oci2c_getreg(i2c, OCI2C_STATUS);

    if (stat & OCI2C_STAT_ARBLOST) {
        // We lost the bus so abort the transfer.  The register definition says
        // the following about this bit:
        //
        //   This bit is set when the core lost arbitration.  Arbitration is
        //   lost when a STOP signal is detected, but not requested, or when
        //   master drives SDA high, but SDA is low.
        //
        PR_DEBUG(DEBUG_ERR,
                 "    ARBLOST error. status=0x%02X, calling"
                 " process=%s, controller=%s, slave=0x%X\n",
                 stat, current->comm, i2c->adap.name, i2c->cur_slave);
        i2c->errno = EPROTO;
        i2c->state = STATE_STOP;
    } else if (i2c->cur_byte == 0 && (stat & OCI2C_STAT_NOACK)) {
        // The device did not acknowledge the START command
        PR_DEBUG(DEBUG_INFO,
                 "    NOACK error on START. status=0x%02X,"
                 " calling process=%s, controller=%s, slave=0x%X\n",
                 stat, current->comm, i2c->adap.name, i2c->cur_slave);
        i2c->errno = ECOMM;
        i2c->state = STATE_STOP;
    } else if (i2c->cur_byte >= msg->len) {
        // There is no data left for this message.  If we did not trace the
        // message at the beginning, trace it now.
        if (DO_DEBUG(DEBUG_MSG) && debug_level == DEBUG_MSG &&
            (msg->flags & I2C_M_RD))
            oci2c_trace_msg(msg, i2c->cur_msg, true);

        // Advance to the next message in the transaction
        oci2c_next_msg(i2c);
    } else if (msg->flags & I2C_M_RD) {
        // Send a READ command and advance to the next state
        int cmd = ((i2c->cur_byte < msg->len - 1) ? OCI2C_CMD_READ
                                                  : OCI2C_CMD_READ_LAST);
        oci2c_cmd(i2c, cmd, 0);
        i2c->state = STATE_READ;
    } else {
        // Send a WRITE command and advance to the next state
        oci2c_cmd(i2c, OCI2C_CMD_WRITE, msg->buf[i2c->cur_byte]);
        i2c->state = STATE_WRITE;
    }
}

// Actions for state READ - waiting to receive data from device
static void
oci2c_read(struct oci2c_controller *i2c) {
    // Read the data byte received
    struct i2c_msg *msg  = &i2c->msg[i2c->cur_msg];
    u8              data = oci2c_getreg(i2c, OCI2C_DATA);
    PR_DEBUG(DEBUG_READ, "    read data 0x%02X\n", data);
    msg->buf[i2c->cur_byte] = data;
    i2c->cur_byte++;
    i2c->state = STATE_DATA;
}

// Actions for state WRITE - waiting for device to acknowledge data
static void
oci2c_write(struct oci2c_controller *i2c) {
    // Check for acknowledgement of last WRITE
    u8 stat = oci2c_getreg(i2c, OCI2C_STATUS);
    if (stat & OCI2C_STAT_NOACK) {
        PR_DEBUG(DEBUG_INFO,
                 "    NOACK error on WRITE. status=0x%02X,"
                 " calling process=%s, controller=%s, slave=0x%X\n",
                 stat, current->comm, i2c->adap.name, i2c->cur_slave);
        i2c->errno = ECOMM;
        i2c->state = STATE_STOP;
    } else {
        i2c->cur_byte++;
        i2c->state = STATE_DATA;
    }
}

// Actions for state STOP - ready to send STOP command for previous sequence
static void
oci2c_stop(struct oci2c_controller *i2c) {
    // Send a STOP command
    oci2c_cmd(i2c, OCI2C_CMD_STOP, 0);

    // If is no error and there are more messages to process, go to the START
    // state.  Otherwise, this was the STOP to terminate the last message.
    if (!i2c->errno && i2c->cur_msg < i2c->num_msg)
        i2c->state = STATE_START;
    else
        i2c->state = STATE_DONE;
}

// Actions for state DONE - transfer done
static void
oci2c_done(struct oci2c_controller *i2c) {
    // Acknowledge any outstanding interrupt
    oci2c_cmd(i2c, OCI2C_CMD_IACK, 0);
    i2c->state = STATE_DONE;
}

// Process the work queue for an I2C controller.  Returns 1 if more work to do
// or 0 if all messages have been processed.
static int
oci2c_process(struct oci2c_controller *i2c) {
    u8 stat;

    if (i2c->errno == ETIMEDOUT || i2c->errno == EPROTO) {
        i2c->state = STATE_STOP;
        oci2c_stop(i2c);
        oci2c_done(i2c);
        return 0;
    }

    // Run the state machine until there is a transaction in progress that we
    // need to wait for.
    while (((stat = oci2c_getreg(i2c, OCI2C_STATUS)) & OCI2C_STAT_TIP) == 0) {
        PR_DEBUG(DEBUG_STATE, "  state is %s, status=0x%02X\n",
                 oci2c_state_name(i2c->state), stat);

        // Do actions for current state
        switch (i2c->state) {
            case STATE_PRE_START:
                oci2c_pre_start(i2c);
                break;
            case STATE_START:
                oci2c_start(i2c);
                break;
            case STATE_DATA:
                oci2c_data(i2c);
                break;
            case STATE_READ:
                oci2c_read(i2c);
                break;
            case STATE_WRITE:
                oci2c_write(i2c);
                break;
            case STATE_STOP:
                oci2c_stop(i2c);
                break;
            case STATE_DONE:
                oci2c_done(i2c);
                return 0;
            case STATE_EXIT:
                return 0;
        }
    }

    return 1;
}

// MSI interrupt handler
static irqreturn_t
oci2c_isr(int vec, void *data) {
    struct oci2c_pci *       pci = (struct oci2c_pci *)data;
    struct oci2c_controller *i2c = pci->i2c;
    u64                      int_pend;
    int                      i;

    // Sanity check for a device that is not configured
    if (pci->base == NULL || i2c == NULL) return IRQ_NONE;

    // Loop while there are any controllers with an interrupt pending.  Note
    // that we clear each controller interrupt bit as we process it, which
    // means the bit could get set again while we are processing other
    // controllers.  Thus, we loop until there are no more bits set.
    //
    // There may also be a race condition where an interrupt bit is set after
    // we exit the loop, but before returning from the ISR.  It is unclear what
    // the hardware will do in that case, but it may not trigger another
    // interrupt.
    while ((int_pend = readq(pci->base + OCI2C_MSI_PEND)) != 0) {
        for (i = 0; int_pend && i < pci->i2c_count; i++) {
            if (int_pend & ((u64)1 << i)) {
                int_pend &= ~((u64)1 << i);
                if (!oci2c_process(&i2c[i])) wake_up(&i2c->wait);
            }
        }
    }

    return IRQ_HANDLED;
}

// Wait for an I2C transaction to complete.  For interrupt-driven mode, this
// will wait until the entire message chain is complete.  For polling mode,
// this will wait until the current command is complete.  Returns non-zero if
// the timeout expired.
static int
oci2c_wait(struct oci2c_controller *i2c) {
    s64 start_time = ktime_to_us(ktime_get());
    s64 last_time  = start_time;
    s64 curr_time  = start_time;
    u8  stat;

    if (i2c->use_isr) {
        // Wait while the interrupt handler processes the entire message chain
        if (!wait_event_timeout(i2c->wait, (i2c->state == STATE_DONE), 2000)) {
            curr_time = ktime_to_us(ktime_get());
            PR_DEBUG(DEBUG_ERR,
                     "    timeout, elapsed=%lldus. calling process=%s\n",
                     curr_time - start_time, current->comm);
            i2c->errno = ETIMEDOUT;
            return ETIMEDOUT;
        }
    } else {
        // Poll for TIP to be deasserted
        while (((stat = oci2c_getreg(i2c, OCI2C_STATUS)) & OCI2C_STAT_TIP)) {
            PR_DEBUG(DEBUG_TIP,
                     "    wait for TIP,  status=0x%02X, elapsed=%lldus, "
                     "total=%lldus\n",
                     stat, curr_time - last_time, curr_time - start_time);

            // Decide how long to sleep while we wait for the device.  A byte
            // typically takes around 100us to transfer, but it can increase
            // due to clock stretching and other factors.  To optimize our
            // wakeups, we sleep ~100us the first time, then ~20us during the
            // window when we expect the typical transfer to complete, then
            // ~250us until timeout.
            if (curr_time - start_time < 80)
                usleep_range(80, 100);
            else if (curr_time - start_time < 200)
                usleep_range(10, 20);
            else if (curr_time - start_time < 2000)
                usleep_range(200, 250);
            else {
                PR_DEBUG(DEBUG_INFO,
                         "    timeout,       status=0x%X, elapsed=%lldus, "
                         "total=%lldus, calling process=%s, controller=%s, "
                         "slave=0x%X\n",
                         stat, curr_time - last_time, curr_time - start_time,
                         current->comm, i2c->adap.name, i2c->cur_slave);

                i2c->errno = ETIMEDOUT;
                return ETIMEDOUT;
            }

            // Check if we slept way past our range.  This is debug code to
            // track jitter in kernel sleep durations.
            last_time = curr_time;
            curr_time = ktime_to_us(ktime_get());
            if (curr_time - last_time > 500)
                PR_DEBUG(DEBUG_INFO,
                         "%s: unusually long sleep of %lldus on %s\n",
                         __FUNCTION__, curr_time - last_time, i2c->adap.name);
        }
    }

    curr_time = ktime_to_us(ktime_get());
    PR_DEBUG(DEBUG_CMD,
             "    transfer done, status=0x%02X, elapsed=%lldus, total=%lldus\n",
             oci2c_getreg(i2c, OCI2C_STATUS), curr_time - last_time,
             curr_time - start_time);

    return 0;
}

// Top-level function to transfer a message chain.
static int
oci2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msg, int num) {
    struct oci2c_controller *i2c = i2c_get_adapdata(adap);

    PR_DEBUG(DEBUG_INFO, "attempting to get an i2c mutex, %s (%s)\n",
             i2c->adap.dev.kobj.name, i2c->adap.name);
    if (!mutex_trylock(&i2c->mutex)) {
        PR_DEBUG(DEBUG_ERR, "failed to get an i2c mutex %s (%s)\n",
                 i2c->adap.dev.kobj.name, i2c->adap.name);
        return -EACCES;
    }

    PR_DEBUG(DEBUG_MSG, "begin %d message transaction on %s (%s)\n", num,
             i2c->adap.dev.kobj.name, i2c->adap.name);

    // Make sure this controller is enabled.  Resetting some devices in the
    // FPGA can have the side effect of also resetting the I2C controller for
    // that device so we need to re-enable when that happens.
    if (!oci2c_is_enabled(i2c)) {
        int rc = oci2c_enable(i2c);
        PR_DEBUG(DEBUG_CMD, "  enable controller\n");
        if (rc) {
            mutex_unlock(&i2c->mutex);
            return -rc;
        }
    }

    // In cases where the I2C controller is locked by the FPGA,
    // any writes to FPGA I2C registers will not go through.
    // Check if I2C Core Enable bit got really set.
    if (!oci2c_is_enabled(i2c)) {
        // The controller could be locked by the FPGA. Bail out.
        PR_DEBUG(DEBUG_CMD, "  controller is disabled\n");
        mutex_unlock(&i2c->mutex);
        return -EACCES;
    }

    // Initialize controller state machine
    i2c->msg       = msg;
    i2c->num_msg   = num;
    i2c->cur_msg   = 0;
    i2c->cur_slave = 0;
    i2c->cur_byte  = 0;
    i2c->errno     = 0;
    i2c->state     = STATE_PRE_START;

    // Process messages until done
    while (oci2c_process(i2c)) {
        oci2c_wait(i2c);

        // This is to test a workaround for the 8121-1149 DAC and possibly
        // others.  That DAC requires a 50us pause between completing one
        // operation and starting the next.  Set the pad_time option to change
        // the value for testing.
        //
        // TODO - we are probably dropping support for the 8121-1149 DAC so
        // this workaround can be removed when that happens.  If we keep it,
        // this workaround is not compatible with interrupt-driven mode.  That
        // mode is designed to issue the next command while processing the
        // completion interrupt from the previous command.  We can't spin 50us
        // in an ISR so we need to insert sleeps, which exposes us to the
        // scheduling jitter we were trying to avoid by using interrupts in the
        // first place.
        //
        if (pad_time > 0) usleep_range(pad_time, pad_time + 10);
    }

    // Ensure that the I2C core is still enabled.
    // There could be cases when the controller got locked in between a
    // transaction.
    if (!oci2c_is_enabled(i2c)) {
        // Bail out.
        PR_DEBUG(DEBUG_CMD, "  controller is disabled\n");
        mutex_unlock(&i2c->mutex);
        return -EACCES;
    }

    // Wait a bit to let the device process the final command.  Some devices
    // need this and some do not.  TODO - investigate whether we should check
    // for BUSY first before adding this delay.
    usleep_range(pad_time + 50, pad_time + 60);

    // Release controller mutex
    mutex_unlock(&i2c->mutex);

    // Return count of the messages we processed or negative to indicate error.
    // The common I2C layer allows partial processing of the message chain, but
    // we always process the entire chain.
    return (i2c->errno ? -i2c->errno : num);
}

static u32
oci2c_func(struct i2c_adapter *adap) {
    return (I2C_FUNC_I2C | I2C_FUNC_NOSTART | I2C_FUNC_SMBUS_EMUL);
}

static const struct i2c_algorithm oci2c_algorithm = {
    .master_xfer = oci2c_xfer, .functionality = oci2c_func,
};

static struct i2c_adapter oci2c_adapter = {
    .owner = THIS_MODULE,
    .name  = "i2c-ocores",
    .class = I2C_CLASS_DEPRECATED,  // don't use classes with this adapter
    .algo  = &oci2c_algorithm,
};

static const struct pci_device_id oci2c_ids[] = {
    {PCI_DEVICE(PCI_VENDOR_ID_PENSANDO, PCI_DEVICE_ID_PENSANDO_FPGA_I2C)},
    {0}};


static struct fpga_i2c_mux_plat_data i2c_mux_plat_data[] = {
	[0] = {
		.mux_reg_addr   = 0,
		.dev_id		= 0,
	},
	[1] = {
		.mux_reg_addr   = 0,
		.dev_id		= 1,
	},
	[2] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[3] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[4] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[5] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[6] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[7] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[8] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[9] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[10] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[11] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[12] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[13] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[14] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[15] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
	[16] = {
		.mux_reg_addr   = 0,
		.dev_id		= 2,
	},
};

struct i2c_board_info  board_info [] = {
	[0] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x01),
	},
	[1] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x02),
	},
	[2] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[3] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[4] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[5] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[6] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[7] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[8] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[9] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[10] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[11] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[12] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[13] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[14] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[15] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
	[16] = {
		I2C_BOARD_INFO("pen_fpga_i2c_mux", 0x03),
	},
}; 

// Initialize all the I2C controllers on a PCI device
static int
oci2c_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    int                      ret      = 0;
    struct oci2c_pci *       pci      = NULL;
    struct oci2c_controller *i2c_list = NULL;
    void __iomem *base_addr;
    void __iomem *const *tbl;
    int                  i2c_count;
    int                  i2c_adapter_count = 0;
    uint32_t             fpga_id;
    u64                  int_mask = 0;
    int                  i;

    // Enable PCI device
    ret = pcim_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable OpenCores I2C PCI device rc=%d\n",
                ret);
        goto err_pci_enable;
    }

    // Set the device as bus master for MSI to work
    if (!pdev->is_busmaster) {
        pci_set_master(pdev);
    }

    // Request BAR and iomap it
    ret =
        pcim_iomap_regions(pdev, OCI2C_BAR_MASK, dev_driver_string(&pdev->dev));
    if (ret) {
        dev_err(&pdev->dev, "Failed to request and iomap PCI region rc=%d\n",
                ret);
        goto err_pci_req;
    }

    // Retrive BAR 0
    tbl       = pcim_iomap_table(pdev);
    base_addr = tbl[0];

    // Clear interrupt pending and enable bits
    writeq(0, base_addr + OCI2C_MSI_PEND);
    writeq(0, base_addr + OCI2C_MSI0_INT_EN);

    // Get FPGA ID
    fpga_id = ((ioread32(base_addr) & 0xFFFF0000) >> 16);

    // Get the number of I2C controllers on this FPGA
    i2c_count = 17; 
    if (i2c_count < 0) {
        dev_err(&pdev->dev, "Unknown FGPA ID 0x%04x, driver init failed\n",
                fpga_id);
        ret = -EINVAL;
        goto err_bad_count;
    }

    // Allocate memory for controller tables
    pci = devm_kzalloc(&pdev->dev, sizeof *pci, GFP_KERNEL);
    if (!pci) {
        ret = -ENOMEM;
        goto err_nomem;
    }
    i2c_list =
        devm_kzalloc(&pdev->dev, sizeof *i2c_list * i2c_count, GFP_KERNEL);
    if (!i2c_list) {
        ret = -ENOMEM;
        goto err_nomem;
    }

    // Initialize PCI table entry
    pci->pdev      = pdev;
    pci->base      = base_addr;
    pci->i2c_count = i2c_count;
    pci->i2c       = i2c_list;

    // Set to true to use interrupt-based processing of I2C commands or false
    // to use polling-based.  CAUTION - the interrupt-based mode is untested.
    pci->use_isr = false;

    // Initialize each controller entry
    printk(KERN_DEBUG "i2c: initializing %d i2c controllers for FPGA 0x%04x\n",
           i2c_count, fpga_id);
    for (i = 0; i < i2c_count; i++) {
        struct oci2c_controller *i2c = &i2c_list[i];

        // Some FPGAs have holes in the controller number space so skip them
        //if (!hpe_fpga_i2c_valid_controller(fpga_id, i)) continue;

        mutex_init(&i2c->mutex);

        // Configure parameters for this controller
        i2c->fpga_id        = fpga_id;
        i2c->controller_num = i;
        i2c->use_isr        = pci->use_isr;
        i2c->pdev           = pdev;
        i2c->base           = (base_addr + OCI2C_BASE_ADDR +
                     (i * OCI2C_REG_CNT * OCI2C_REG_IO_WIDTH));

        // Initialize waitqueue if using interrupts
        if (i2c->use_isr) init_waitqueue_head(&i2c[i].wait);

        // Hook up driver to tree
        i2c->adap = oci2c_adapter;
        i2c_set_adapdata(&i2c->adap, i2c);
        i2c->adap.dev.parent  = &pdev->dev;
        i2c->adap.dev.of_node = pdev->dev.of_node;

        // Format the adapter name.  Note that the .rules file matches on this
        // name so the format must be consistent between them.
        snprintf(i2c->adap.name, sizeof i2c->adap.name,
                 "I2C_FPGA_%04x_adap_%02d @ %016llx", fpga_id, i,
                 virt_to_phys(i2c->base));

        // Add I2C adapter to I2C tree
        printk(KERN_DEBUG "Instantiating i2c controller %s\n", i2c->adap.name);
        ret = i2c_add_adapter(&i2c->adap);
        if (ret) {
            dev_err(&pdev->dev, "Failed to add adapter %s rc=%d\n",
                    i2c->adap.name, ret);
            goto err_add_adapter;
        }
        i2c_adapter_count++;
        printk(KERN_DEBUG "i2c: device %s created for %s\n",
               i2c->adap.dev.kobj.name, i2c->adap.name);

        // Instantiate any devices for this controller that have a
        // device-specific driver.
        //hpe_fpga_i2c_add_devices(&i2c->adap, fpga_id, i, &i2c->addr_table,
         //                        &i2c->addr_table_len);
	i2c_mux_plat_data[i].mux_reg_addr = pdev->resource[0].start + 
		OCI2C_BASE_ADDR + (i * OCI2C_REG_CNT * OCI2C_REG_IO_WIDTH) + 0x14;
	i2c_mux_plat_data[i].dev_id = i; 
	board_info[i].platform_data = (void *) &i2c_mux_plat_data[i]; 
	i2c_new_device(&i2c->adap, &board_info[i]);
        // Set MSI interrupt mask bit for this controller
        int_mask |= (1 << i);
    }

    // Initialize interrupt handling
    if (pci->use_isr) {
        // Program the interrupt mask register
        writeq(int_mask, base_addr + OCI2C_MSI0_INT_EN);

        // Enable MSI interrupts for device
        ret = pci_enable_msi(pdev);
        if (ret) {
            dev_err(&pdev->dev, "Failed to enable MSI interrupts rc=%d\n", ret);
            goto err_enable_msi;
        }

        // Register interrupt handler for this device
        ret = devm_request_irq(&pdev->dev, pdev->irq, oci2c_isr, 0,
                               "ocores-isr", pci);
        if (ret) {
            dev_err(&pdev->dev, "Failed to request IRQ rc=%d\n", ret);
            goto err_request_irq;
        }
    }

    // Register the PCI device
    pci_set_drvdata(pdev, pci);

    // Return success
    return 0;

// Error unwind and return
err_request_irq:
    pci_disable_msi(pdev);
err_enable_msi:
err_add_adapter:
    for (i = 0; i < i2c_adapter_count; i++) {
        i2c_del_adapter(&i2c_list[i].adap);
        if (i2c_list[i].addr_table)
            devm_kfree(&pdev->dev, i2c_list[i].addr_table);
    }
err_nomem:
    if (i2c_list) devm_kfree(&pdev->dev, i2c_list);
    if (pci) devm_kfree(&pdev->dev, pci);
err_bad_count:
    pcim_iounmap_regions(pdev, OCI2C_BAR_MASK);
err_pci_req:
    pci_disable_device(pdev);
err_pci_enable:
    return ret;
}

// Remove a PCI device
static void
oci2c_remove(struct pci_dev *pdev) {
    struct oci2c_pci *       pci      = pci_get_drvdata(pdev);
    struct oci2c_controller *i2c_list = pci->i2c;
    int                      i;

    for (i = 0; i < pci->i2c_count; i++) {
        struct oci2c_controller *i2c = &i2c_list[i];
        if (i2c->base == NULL) {
            continue;
        }
        // Lock the controller so no transactions are in progress
        mutex_lock(&i2c->mutex);
        oci2c_disable(i2c);
        i2c_del_adapter(&i2c->adap);
        if (i2c->addr_table) {
            devm_kfree(&pdev->dev, i2c->addr_table);
        }
    }

    if (pci->use_isr) {
        pci_disable_msi(pdev);
        devm_free_irq(&pdev->dev, pdev->irq, pci);
    }
    pcim_iounmap_regions(pdev, OCI2C_BAR_MASK);
    pci_disable_device(pdev);
    pci->base = NULL;
    devm_kfree(&pdev->dev, i2c_list);
    devm_kfree(&pdev->dev, pci);
}

/* PCI bus error detected error handler */
static pci_ers_result_t
hpe_fpga_i2c_error_detected_err_handler(struct pci_dev *       pdev,
                                        enum pci_channel_state chstate) {
    dev_err(&pdev->dev, "%s \n", __func__);
    //      if channel state is pci_channel_io_normal then
    //      link reset at upstream is not needed.This is non
    //      correctable, non fatal error. if its pci_channel_io_frozen
    //      then performing link reset at upstream is needed as this
    //      this is non-correctable fatal error.

    // TODO: Reset the specific i2c controller on MCBE fpga
    //      as the device is still available. For LCB/LCIOX
    //      nothing can be done as the device is removed
    //      and driver will be unloaded. If the error is triggered
    //      for a specific txn and device is still present
    //      then we need to decide on what needs to be done
    return PCI_ERS_RESULT_NONE;
}

/* PCI MMIO enable error handler */
static pci_ers_result_t
hpe_fpga_i2c_mmio_enabled_err_handler(struct pci_dev *pdev) {
    dev_err(&pdev->dev, "%s \n", __func__);
    return PCI_ERS_RESULT_NONE;
}

/* PCI slot reset error handler */
static pci_ers_result_t
hpe_fpga_i2c_slot_reset_err_handler(struct pci_dev *pdev) {
    dev_err(&pdev->dev, "%s \n", __func__);
    //      TODO:
    //      need to identify if we hit this scenario
    //      and decide on course of action
    return PCI_ERS_RESULT_NONE;
}

/* PCI resume error handler */
static void
hpe_fpga_i2c_resume_err_handler(struct pci_dev *pdev) {
    dev_err(&pdev->dev, "%s \n", __func__);
}

static struct pci_error_handlers ocores_i2c_pci_err_handler = {
    .error_detected = hpe_fpga_i2c_error_detected_err_handler,
    .mmio_enabled   = hpe_fpga_i2c_mmio_enabled_err_handler,
    .slot_reset     = hpe_fpga_i2c_slot_reset_err_handler,
    .resume         = hpe_fpga_i2c_resume_err_handler,
};

static struct pci_driver oci2c_driver = {
    .name        = "ocores-i2c",
    .id_table    = oci2c_ids,
    .probe       = oci2c_probe,
    .remove      = oci2c_remove,
    .err_handler = &ocores_i2c_pci_err_handler,
    .suspend     = NULL,
    .resume      = NULL,
};

static int __init
           oci2c_init(void) {
    return pci_register_driver(&oci2c_driver);
}

static void __exit
            oci2c_exit(void) {
    pci_unregister_driver(&oci2c_driver);
}

module_init(oci2c_init);
module_exit(oci2c_exit);

MODULE_DESCRIPTION("PCIe OpenCores I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ocores-i2c");

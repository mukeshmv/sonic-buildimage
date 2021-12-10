import os
import commands
import sys
import logging
import time

kmod  = [
        'modprobe i2c-mux',
        'modprobe i2c-dev',
        'modprobe at24',
        'insmod /lib/modules/{}/extra/fpga_func1.ko',
        'insmod /lib/modules/{}/extra/fpga_func2.ko',
        'insmod /lib/modules/{}/extra/fpga_i2c_mux.ko',
        'insmod /lib/modules/{}/extra/fpga_i2c_ocores.ko',
        ]

PCI_FPGA_FUNC1_PATH = '/sys/bus/pci/devices/0000\:12\:00.1/'
PCI_FPGA_FUNC2_PATH = '/sys/bus/pci/devices/0000\:12\:00.2/'
PCI_RESCAN_PATH = '/sys/bus/pci/rescan'

sfp_map =  [ 17, 18, 19, 20, 22, 23, 24, 25, 
             27, 28, 29, 30, 32, 33, 34, 35,
             37, 38, 39, 40, 42, 43, 44, 45,
             47, 48, 49, 50, 52, 53, 54, 55,
             57, 58, 59, 60, 62, 63, 64, 65,
             67, 68, 69, 70, 72, 73, 74, 75,
             77, 78, 79, 80, 82, 83, 84, 85
           ]
def device_install():
    for i in range(0, len(sfp_map)):
        cmd = "echo optoe2 0x50 > /sys/class/i2c-adapter/i2c-"+str(sfp_map[i])+"/new_device"
        status, output = commands.getstatusoutput(cmd)
        

def install_mod():

    kernel_rel = commands.getoutput('uname -r')
  
    for i in range(0, len(kmod)):
        cmd = kmod[i].format(kernel_rel)
        status, output = commands.getstatusoutput(cmd)
        if status:
          print('Failed :'+cmd)

def fru_eeprom_install():
    cmd = "echo 24C256 0x50 > /sys/class/i2c-adapter/i2c-12/new_device"
    status, output = commands.getstatusoutput(cmd)
    time.sleep(0.1)
    cmd = "echo 24C256 0x51 > /sys/class/i2c-adapter/i2c-12/new_device"
    status, output = commands.getstatusoutput(cmd)
    time.sleep(0.1)


def pwr_on_td3():
    cmd = 'echo 1 > ' + PCI_FPGA_FUNC1_PATH + 'td3_pwr_ctrl'
    status, output = commands.getstatusoutput(cmd)

def pwr_on_elba1():
    cmd = 'echo 1 > ' + PCI_FPGA_FUNC2_PATH + 'elba1_pwr_ctrl'
    status, output = commands.getstatusoutput(cmd)
    cmd = 'echo 1 > ' + PCI_FPGA_FUNC1_PATH + 'elba1_pwr_ctrl'
    status, output = commands.getstatusoutput(cmd)

def pwr_on_elba0():
    cmd = 'echo 1 > ' + PCI_FPGA_FUNC2_PATH + 'elba0_pwr_ctrl'
    status, output = commands.getstatusoutput(cmd)
    cmd = 'echo 1 > ' + PCI_FPGA_FUNC1_PATH + 'elba0_pwr_ctrl'
    status, output = commands.getstatusoutput(cmd)

def main():
    install_mod()
    pwr_on_elba0()
    pwr_on_elba1()
    pwr_on_td3()
    time.sleep(2)
    cmd = 'echo 1 > ' + PCI_RESCAN_PATH 
    status, output = commands.getstatusoutput(cmd)

if __name__ == "__main__":
    main()


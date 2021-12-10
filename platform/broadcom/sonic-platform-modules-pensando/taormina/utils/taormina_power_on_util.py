#!/usr/bin/env python
# coding: utf-8

import os
import commands
import sys
import logging
import time

PCI_FPGA_FUNC1_PATH = '/sys/bus/pci/devices/0000\:12\:00.1/'
PCI_FPGA_FUNC2_PATH = '/sys/bus/pci/devices/0000\:12\:00.2/'
PCI_RESCAN_PATH = '/sys/bus/pci/rescan'


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
    '''
    cmd = 'ls ' + PCI_FPGA_FUNC1_PATH 
    status, output = commands.getstatusoutput(cmd)
    print str(output)
    '''
    pwr_on_elba0()
    pwr_on_elba1()
    pwr_on_td3()
    cmd = 'echo 1 > ' + PCI_RESCAN_PATH 
    status, output = commands.getstatusoutput(cmd)

if __name__ == "__main__":
    main()


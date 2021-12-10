# -*- coding: utf-8 -*-

#############################################################################
# Ruijie B6510-48VS8CQ
#
# Module contains an implementation of SONiC Platform Base API and
# provides the platform information
#
#############################################################################

try:
    import time
    import subprocess
    from sonic_platform_base.chassis_base import ChassisBase
    from sonic_platform.common import Common
    from sonic_platform.sfp import Sfp
    from sonic_platform.sfp import PORT_START
    from sonic_platform.sfp import PORTS_IN_BLOCK
    from sonic_platform.logger import logger
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

class Chassis(ChassisBase):
    """
    Platform-specific Chassis class
    """
    def __init__(self):
        ChassisBase.__init__(self)
        self.__initialize_eeprom()

    def __initialize_eeprom(self):
        from sonic_platform.eeprom import Eeprom
        self._eeprom = Eeprom()


    def get_name(self):
        """
        Retrieves the name of the chassis
        Returns:
            string: The name of the chassis
        """
        return self._eeprom.modelstr()

    def get_presence(self):
        """
        Retrieves the presence of the chassis
        Returns:
            bool: True if chassis is present, False if not
        """
        return True

    def get_model(self):
        """
        Retrieves the model number (or part number) of the chassis
        Returns:
            string: Model/part number of chassis
        """
        return self._eeprom.part_number_str()

    def get_serial(self):
        """
        Retrieves the serial number of the chassis (Service tag)
        Returns:
            string: Serial number of chassis
        """
        return self._eeprom.serial_str()

    def get_status(self):
        """
        Retrieves the operational status of the chassis
        Returns:
            bool: A boolean value, True if chassis is operating properly
            False if not
        """
        return True

    def get_base_mac(self):
        """
        Retrieves the base MAC address for the chassis

        Returns:
            A string containing the MAC address in the format
            'XX:XX:XX:XX:XX:XX'
        """
        return self._eeprom.base_mac_addr()

    def get_system_eeprom_info(self):
        """
        Retrieves the full content of system EEPROM information for the chassis
        Returns:
            A dictionary where keys are the type code defined in
            OCP ONIE TlvInfo EEPROM format and values are their corresponding
            values.
        """
        return self._eeprom.system_eeprom_info()

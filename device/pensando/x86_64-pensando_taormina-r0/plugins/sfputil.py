# sfputil.py
#
# Platform-specific SFP transceiver interface for SONiC
#

try:
    import time
    from sonic_sfp.sfputilbase import SfpUtilBase
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))


class SfpUtil(SfpUtilBase):
    """Platform-specific SfpUtil class"""

    PORT_START = 0
    QSFP_START = 48
    PORT_END = 56 
    PORTS_IN_BLOCK = 56 

    _port_to_i2c_mapping = {
	    1:  17,
	    2:  18,
	    3:  19,
	    4:  20,
	    5:  22,
	    6:  23,
	    7:  24,
	    8:  25,
	    9:  27,
	    10: 28,
	    11: 29,
	    12: 30,
	    13: 32,
	    14: 33,
	    15: 34,
	    16: 35,
	    17: 37,
	    18: 38,
	    19: 39,
	    20: 40,
	    21: 42,
	    22: 43,
	    23: 44,
	    24: 45,
            25: 47, 
            26: 48,
            27: 49,
            28: 50,
            29: 52,
            30: 53,
            31: 54,
            32: 55,
            33: 57,
            34: 58,
            35: 59,
            36: 60,
            37: 62,
            38: 63,
            39: 64,
            40: 65,
            41: 67,
            42: 68,
            43: 69,
            44: 70,
            45: 72,
            46: 73,
            47: 74,
            48: 75,
            49: 77,
            50: 78,
            51: 79,
            52: 80,
            53: 82,
            54: 83,
            55: 84,
            56: 85,
	}

    _port_to_eeprom_mapping = {}

    @property
    def port_start(self):
        return self.PORT_START

    @property
    def port_start_qsfp(self):
        return self.PORT_START_QSFP

    @property
    def port_end(self):
        return self.PORT_END

    @property
    def qsfp_ports(self):
        return list(range(self.QSFP_START, self.PORTS_IN_BLOCK + 1))

    @property
    def port_to_eeprom_mapping(self):
        return self._port_to_eeprom_mapping

    def __init__(self):
        eeprom_path = "/sys/class/i2c-adapter/i2c-{0}/{0}-0050/eeprom"

        for x in range(0, self.port_end + 1):
            self._port_to_eeprom_mapping[x] = eeprom_path.format(self._port_to_i2c_mapping[x])
        SfpUtilBase.__init__(self)

    def get_response(self, port_num):
        return True

    def get_presence(self, port_num):
        return True

    def get_low_power_mode(self, port_num):
        return False

    def set_low_power_mode(self, port_num, lpmode):
        return False

    def reset(self, port_num):
        return False

    def get_transceiver_change_event(self):
        """
        TODO: This function need to be implemented
        when decide to support monitoring SFP(Xcvrd)
        on this platform.
        """
        raise NotImplementedError

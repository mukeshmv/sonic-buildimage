import sys

try:
   from sonic_platform_base.platform_base import PlatformBase
   from sonic_platform.chassis import Chassis
except ImportError as e:
   raise ImportError("%s - required module not found" % e)

class Platform(PlatformBase):
    def __init__(self):
	PlatformBase.__init__(self)
        self._chassis = Chassis()

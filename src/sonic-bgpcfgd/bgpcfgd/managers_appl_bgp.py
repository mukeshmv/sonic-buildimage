import traceback
from .log import log_crit, log_err, log_debug
from .manager import Manager
from .template import TemplateFabric
import socket
from swsscommon import swsscommon

class AppBgpGlobal(Manager):
    """ This class handles BGP Global table updates on APPL_DB """
    def __init__(self, common_objs, db, table):
        """
        Initialize the object
        :param common_objs: common object dictionary
        :param db: name of the db
        :param table: name of the table in the db
        """
        super(AppBgpGlobal, self).__init__(
            common_objs,
            [],
            db,
            table,
        )

        log_debug("Registering table {} on db {}".format(table, db))
        self.bgp_enabled = False
        self.local_asn = 0

    def set_handler(self, key, data):
        log_debug("BGP_GLOBAL set")
        # BGP non-default VRF support not implemented
        vrf = key
        if vrf != "default":
            log_err("VRF must be default")
            return False

        cmd_list = []
        for key, val in data.items():
            if key == 'local_asn':
                cmd_list.append("router bgp {}".format(val))
                if self.bgp_enabled:
                    if self.local_asn != val:
                        ## TODO support ASN change
                        log_err("local_asn change is not allowed cur {} new {}".format(self.local_asn, val))
                        return False
                else:
                    cmd_list.append("no bgp default ipv4-unicast")
                    self.bgp_enabled = True
                    self.local_asn = val
            if key == 'router_id':
                cmd_list.append("bgp router-id {}".format(val))

        if not self.bgp_enabled:
            log_err("local_asn missing in BGP_GLOBAL table")
            return False

        log_debug("Pushing cmds: {}".format(str(cmd_list)))
        self.cfg_mgr.push_list(cmd_list)
        return True

    def del_handler(self, key):
        log_debug("BGP_GLOBAL del")
        # BGP non-default VRF support not implemented
        vrf = key
        if vrf != "default":
            log_err("VRF must be default")
            return False

        if not self.bgp_enabled:
            log_err("BGP delete failed, BGP not created yet")
            return False

        cmd_list = []
        cmd_list.append("no router bgp {}".format(self.local_asn))
        log_debug("Pushing cmds: {}".format(str(cmd_list)))
        self.cfg_mgr.push_list(cmd_list)
        return True

class AppBgpNeighbor(Manager):
    """ This class handles BGP Neighbor table updates on APPL_DB """
    def __init__(self, common_objs, db, table):
        """
        Initialize the object
        :param common_objs: common object dictionary
        :param db: name of the db
        :param table: name of the table in the db
        """
        deps = [
            ("APPL_DB", "BGP_GLOBAL", "default/local_asn")
            ]
        super(AppBgpNeighbor, self).__init__(
            common_objs,
            deps,
            db,
            table,
        )
        log_debug("Registering table {} on db {}".format(table, db))

        self.bgp_enabled = False
        self.local_asn = 0

    def set_handler(self, key, data):
        log_debug("BGP_NEIGHBOR set")
        # BGP non-default VRF support not implemented
        vrf, nbr = self.split_key(key)
        if vrf != "default":
            log_err("VRF must be default")
            return False

        if not self.bgp_enabled:
            # fetch the local asn
            try:
                self.local_asn = self.directory.get_slot("APPL_DB", "BGP_GLOBAL")["default"]["local_asn"]
                self.bgp_enabled = True
                log_debug("local_asn {} found in BGP_GLOBAL table".format(self.local_asn))
            except KeyError:
                log_err("local_asn missing in BGP_GLOBAL table err")
                return False

        cmd_list = []
        cmd_list.append("router bgp {}".format(self.local_asn))

        for key, val in data.items():
            if key == 'asn':
                cmd_list.append("neighbor {} remote-as {}".format(nbr, val))

        log_debug("Pushing cmds: {}".format(str(cmd_list)))
        self.cfg_mgr.push_list(cmd_list)
        return True

    def del_handler(self, key):
        log_debug("BGP_NEIGHBOR del")
        # BGP non-default VRF support not implemented
        vrf, nbr = self.split_key(key)
        if vrf != "default":
            log_err("VRF must be default")
            return False

        if not self.bgp_enabled:
            log_err("BGP delete failed, BGP not created yet")
            return False

        cmd_list = []
        cmd_list.append("router bgp {}".format(self.local_asn))
        cmd_list.append("no neighbor {}".format(nbr))
        log_debug("Pushing cmds: {}".format(str(cmd_list)))
        self.cfg_mgr.push_list(cmd_list)
        return True

    @staticmethod
    def split_key(key):
        """
        Split key into vrf name and neighbor.
        :param key: key to split
        :return: vrf name extracted from the key, ip prefix extracted from the key
        """
        if ':' not in key:
            return 'default', key
        else:
            return tuple(key.split(':', 1))

    def generate_command(self, op, ip_nh, ip_prefix, vrf):
        return '{}{} route {}{}{}'.format(
            'no ' if op == self.OP_DELETE else '',
            'ipv6' if ip_nh.af == socket.AF_INET6 else 'ip',
            ip_prefix,
            ip_nh,
            ' vrf {}'.format(vrf) if vrf != 'default' else ''
        )


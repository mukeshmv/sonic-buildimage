from collections import defaultdict
from swsscommon import swsscommon

from .log import log_debug, log_crit


g_run = True


def signal_handler(_, __):  # signal_handler(signum, frame)
    """ signal handler """
    global g_run
    g_run = False


class Runner(object):
    """ Implements main io-loop of the application
        It will run event handlers inside of Manager objects
        when corresponding db/table is updated
    """
    SELECT_TIMEOUT = 1000

    def __init__(self, cfg_manager, directory):
        """ Constructor """
        self.cfg_manager = cfg_manager
        self.directory = directory
        self.db_connectors = {}
        self.selector = swsscommon.Select()
        self.callbacks = defaultdict(lambda: defaultdict(list))  # db -> table -> handlers[]
        self.subscribers = set()
        for db_name, table_name in self.directory.get_subscription():
            log_debug("Directory subscription -> {} {}".format(db_name, table_name))
            db_id = swsscommon.SonicDBConfig.getDbId(db_name)
            if db_id not in self.db_connectors:
                self.db_connectors[db_id] = swsscommon.DBConnector(db_name, 0)
            if table_name not in self.callbacks[db_id]:
                conn = self.db_connectors[db_id]
                subscriber = swsscommon.SubscriberStateTable(conn, table_name)
                self.subscribers.add(subscriber)
                self.selector.addSelectable(subscriber)
                log_debug("Add subscriber for table {} in DB {}".format(table_name, db_name))
            self.callbacks[db_id][table_name].append(
                    lambda key, op, data:
                     directory.put(db_name, table_name, key, data)
                     if op == swsscommon.SET_COMMAND
                    else directory.remove(db_name, table_name, key))
            log_debug("Add callback for table {} in DB {}".format(table_name, db_name))

    def add_manager(self, manager):
        """
        Add a manager to the Runner.
        As soon as new events will be receiving by Runner,
        handlers of corresponding objects will be executed
        :param manager: an object implementing Manager
        """
        db_name = manager.get_database()
        table_name = manager.get_table_name()
        db = swsscommon.SonicDBConfig.getDbId(db_name)
        if db not in self.db_connectors:
            self.db_connectors[db] = swsscommon.DBConnector(db_name, 0)

        if table_name not in self.callbacks[db]:
            conn = self.db_connectors[db]
            subscriber = swsscommon.SubscriberStateTable(conn, table_name)
            self.subscribers.add(subscriber)
            self.selector.addSelectable(subscriber)
            log_debug("Add subscriber for table {} in DB {}".format(table_name, db_name))
        self.callbacks[db][table_name].append(manager.handler)
        log_debug("Add callback for table {} in DB {}".format(table_name, db_name))

    def run(self):
        """ Main loop """
        while g_run:
            state, _ = self.selector.select(Runner.SELECT_TIMEOUT)
            if state == self.selector.TIMEOUT:
                continue
            elif state == self.selector.ERROR:
                raise Exception("Received error from select")

            for subscriber in self.subscribers:
                while True:
                    key, op, fvs = subscriber.pop()
                    if not key:
                        break
                    log_debug("Received message : '%s'" % str((key, op, fvs)))
                    for callback in self.callbacks[subscriber.getDbConnector().getDbId()][subscriber.getTableName()]:
                        callback(key, op, dict(fvs))
            rc = self.cfg_manager.commit()
            if not rc:
                log_crit("Runner::commit was unsuccessful")

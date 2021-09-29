#!/bin/bash

echo "starting NetAgent cfg"

export SONIC_MODE=1 

echo $PDS_HOST
echo $SONIC_MODE

exec /usr/bin/netagentcfg > /tmp/netagentcfg_out.log 2> /tmp/netagentcfg_err.log


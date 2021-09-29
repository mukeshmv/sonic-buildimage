#!/bin/bash

echo "starting NetAgent HAL"
export SONIC_MODE=1 

echo $PDS_HOST
echo $SONIC_MODE

exec /usr/bin/netagenthal > /tmp/netagenthal_out.log 2> /tmp/netagenthal_err.log


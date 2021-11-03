#!/bin/bash

	echo CONTROLLER: $CONTROLLER
	echo SERIAL_NUM: $SERIAL_NUM
	echo MAC_ADDR: $MAC_ADDR
	
	sed -i s/CONTROLLER/${CONTROLLER}/ /tmp/template_nmd.json
	sed -i s/MAC_ADDR/${MAC_ADDR}/ /tmp/template_nmd.json
	sed -i s/HOST_NAME/${HOST_NAME}/ /tmp/template_nmd.json
	mv /tmp/template_nmd.json /tmp/nmd.json
	
	sed -i s/SERIAL_NUM/${SERIAL_NUM}/ /tmp/template_fru.json
	sed -i s/MAC_ADDR/${MAC_ADDR}/ /tmp/template_fru.json
	mv /tmp/template_fru.json /tmp/fru.json
    mv /usr/bin/fru.json /tmp/fru.json
	echo "Starting NMD"
	NAPLES_PIPELINE=apollo SONIC_MODE=1 /usr/bin/nmd > /tmp/nmd_out.log 2> /tmp/nmd_err.log &
	sleep 30
	curl -X POST -H "Content-Type: application/json" -k -d @/tmp/nmd.json https://localhost:8888/api/v1/naples/
	sleep 20
	curl 127.0.0.1:9007/api/mode/

while :; do sleep 1; done

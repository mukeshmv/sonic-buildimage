.ONESHELL:
SHELL = /bin/bash
.SHELLFLAGS += -e

$(info executing pensando makefile [${DEST}]...)
MAIN_TARGET = $(PENSANDO_SW)
#DERIVED_TARGET = $(PENSANDO_SW_DBG)

#unexport DEST

$(addprefix $(DEST)/, $(MAIN_TARGET)): $(DEST)/% :
	pushd sw
	# Build package
	DEB_BUILD_OPTIONS=nocheck dpkg-buildpackage -rfakeroot -b -us -uc -j$(SONIC_CONFIG_MAKE_JOBS) --admindir $(SONIC_DPKG_ADMINDIR)
	
	popd
	mv $* $(DEST)/
	#mv $(DERIVED_TARGET) $* $(DEST)/

#$(addprefix $(DEST)/, $(DERIVED_TARGET)): $(DEST)/% : $(DEST)/$(MAIN_TARGET)

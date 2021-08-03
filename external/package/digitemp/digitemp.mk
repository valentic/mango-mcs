#############################################################
#
# digitemp
#
#############################################################

DIGITEMP_VERSION=3.6.0
DIGITEMP_SOURCE:=digitemp-$(DIGITEMP_VERSION).tar.gz
DIGITEMP_SITE:=http://www.digitemp.com/software/linux
DIGITEMP_LICENSE = GPLv2
DIGITEMP_LICENSE_FILES = COPYING
DIGITEMP_DEPENDENCIES += libusb libusb-compat

define DIGITEMP_BUILD_CMDS
	$(MAKE) -C $(DIGITEMP_DIR) CC=$(TARGET_CC) ds9097u
	$(MAKE) -C $(DIGITEMP_DIR) CC=$(TARGET_CC) ds9097
	$(MAKE) -C $(DIGITEMP_DIR) CC=$(TARGET_CC) ds2490
endef

define DIGITEMP_INSTALL_TARGET_CMDS
	cp $(DIGITEMP_DIR)/digitemp_DS9097 $(TARGET_DIR)/usr/bin
	cp $(DIGITEMP_DIR)/digitemp_DS9097U $(TARGET_DIR)/usr/bin
	cp $(DIGITEMP_DIR)/digitemp_DS2490 $(TARGET_DIR)/usr/bin
endef

$(eval $(generic-package))


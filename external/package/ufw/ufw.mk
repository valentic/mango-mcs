################################################################################
#
# ufw 
#
################################################################################

UFW_VERSION = 0.36 
UFW_SOURCE = ufw-$(UFW_VERSION).tar.gz
UFW_SITE = https://launchpad.net/ufw/$(UFW_VERSION)/$(UFW_VERSION)/+download
UFW_SETUP_TYPE = distutils
UFW_LICENSE = GPL-3.0+
UFW_LICENSE_FILES = COPYING

define UFW_CLEAN_INSTALL_DATA
	$(RM) -f $(TARGET_DIR)/usr/share/ufw/messages/*
endef

UFW_POST_INSTALL_TARGET_HOOKS += UFW_CLEAN_INSTALL_DATA

define UFW_SET_ENABLED
	$(SED) 's/ENABLED=no/ENABLED=yes/' $(TARGET_DIR)/etc/ufw/ufw.conf
endef

ifeq ($(BR2_PACKAGE_UFW_ENABLED),y)
UFW_POST_INSTALL_TARGET_HOOKS += UFW_SET_ENABLED
endif

define UFW_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 755 $(@D)/doc/initscript.example $(TARGET_DIR)/etc/init.d/S41ufw
endef

define UFW_INSTALL_INIT_SYSTEMD
    $(INSTALL) -D -m 644 $(@D)/doc/systemd.example \
        $(TARGET_DIR)/usr/lib/systemd/system/ufw.service
    mkdir -p $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
    ln -sf ../../../../usr/lib/systemd/system/ufw.service \
        $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/ufw.service
endef

$(eval $(python-package))

#############################################################
#
# set-dynamics-mode 
#
#############################################################

SET_DYNAMICS_MODE_VERSION = 1.0
SET_DYNAMICS_MODE_SITE = $(SET_DYNAMICS_MODE_PKGDIR)src 
SET_DYNAMICS_MODE_SITE_METHOD = local
SET_DYNAMICS_MODE_LICENSE = PROPRIETARY 

define SET_DYNAMICS_MODE_BUILD_CMDS
	$(MAKE) CC=$(TARGET_CC) -C $(SET_DYNAMICS_MODE_DIR) 
endef

define SET_DYNAMICS_MODE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/set_dynamics_mode -t $(TARGET_DIR)/usr/local/bin
	$(INSTALL) -D -m 0755 $(@D)/device-hook -t $(TARGET_DIR)/etc/gpsd
endef

$(eval $(generic-package))


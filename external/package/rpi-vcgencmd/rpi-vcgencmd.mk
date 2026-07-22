RPI_VCGENCMD_VERSION = 45a0022ac64b4d0788def3c5230c972430f6fc23 
RPI_VCGENCMD_SITE = $(call github,raspberrypi,userland,$(RPI_VCGENCMD_VERSION))
RPI_VCGENCMD_LICENSE = BSD-3-Clause
RPI_VCGENCMD_LICENSE_FILES = LICENCE

RPI_VCGENCMD_CONF_OPTS = 

ifeq ($(BR2_aarch64),y)
RPI_VCGENCMD_CONF_OPTS += -DARM64=ON
endif

# Surgical target installation to keep your image tiny
define RPI_VCGENCMD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/bin/vcgencmd $(TARGET_DIR)/usr/bin/vcgencmd
	$(INSTALL) -D -m 0755 $(@D)/build/lib/libvchiq_arm.so $(TARGET_DIR)/usr/lib/libvchiq_arm.so
	$(INSTALL) -D -m 0755 $(@D)/build/lib/libvcos.so $(TARGET_DIR)/usr/lib/libvcos.so
	$(INSTALL) -D -m 0755 $(@D)/build/lib/libvchostif.so $(TARGET_DIR)/usr/lib/libvchostif.so
endef

$(eval $(cmake-package))


#############################################################
#
# ts-utils-7250V2
#
# ftp://ftp.embeddedarm.com/ts-arm-sbc/ts-7250-v2-linux/sources/ 
# 
#############################################################

TS_UTILS_7250V2_VERSION = 1.0
TS_UTILS_7250V2_SITE = $(TS_UTILS_7250V2_PKGDIR)src 
TS_UTILS_7250V2_SITE_METHOD = local
TS_UTILS_7250V2_LICENSE = PROPRIETARY 

define TS_UTILS_7250V2_BUILD_CMDS
	$(MAKE) CC=$(TARGET_CC) -C $(TS_UTILS_7250V2_DIR) 
endef

define TS_UTILS_7250V2_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tshwctl $(TARGET_DIR)/bin
	$(INSTALL) -D -m 0755 $(@D)/xuartctl $(TARGET_DIR)/bin
	$(INSTALL) -D -m 0755 $(@D)/evgpioctl $(TARGET_DIR)/bin
endef

$(eval $(generic-package))


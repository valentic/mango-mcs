#############################################################
#
# mgetty
#
#############################################################

MGETTY_VERSION = 1.2.1
MGETTY_SITE = ftp://mgetty.greenie.net/pub/mgetty/source/1.2
MGETTY_SOURCE = mgetty-$(MGETTY_VERSION).tar.gz
MGETTY_LICENSE = GPL 
MGETTY_LICENSE_FILES = COPYING

define MGETTY_BUILD_CMDS
	cp $(MGETTY_PKGDIR)/policy.h $(@D)
	$(MAKE) CONFDIR=/etc/mgetty+sendfax CC=$(TARGET_CC) -C $(MGETTY_DIR) mgetty
endef

define MGETTY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mgetty $(TARGET_DIR)/sbin
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/ppp
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/mgetty+sendfax
	$(INSTALL) -m 0644 $(MGETTY_PKGDIR)/dialin.config $(TARGET_DIR)/etc/mgetty+sendfax
	$(INSTALL) -m 0644 $(MGETTY_PKGDIR)/login.config $(TARGET_DIR)/etc/mgetty+sendfax
	$(INSTALL) -m 0644 $(MGETTY_PKGDIR)/mgetty.config $(TARGET_DIR)/etc/mgetty+sendfax
	$(INSTALL) -m 0644 $(MGETTY_PKGDIR)/options.server $(TARGET_DIR)/etc/ppp
endef

$(eval $(generic-package))


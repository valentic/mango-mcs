################################################################################
#
# inn2
#
################################################################################

INN2_VERSION = 2.6.3 
INN2_SITE = ftp://ftp.isc.org/isc/inn
INN2_SOURCE = inn-$(INN2_VERSION).tar.gz
INN2_LICENSE = GPLv2+ and BSD and MIT and Public Domain 
INN2_LICENSE_FILES = LICENSE 
INN2_DEPENDENCIES += openssl host-perl zlib bzip2

INN2_BASEDIR=/opt/news

# Disable parallel make 
INN2_MAKE = $(MAKE1)

INN2_CONF_OPTS = --without-sendmail \
	--prefix=$(INN2_BASEDIR)/ \
	--exec-prefix=$(INN2_BASEDIR)/ \
	--sysconfdir=$(INN2_BASEDIR)/etc/ \
	--enable-largefiles \
	--with-news-user=news \
	--with-news-group=news \
	--with-news-master=news 

# We need to run the package's autogen to pick up patch changes
define INN2_RUN_AUTOGEN
	(cd $(@D); ./autogen)
endef

INN2_PRE_CONFIGURE_HOOKS += INN2_RUN_AUTOGEN

define INN2_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(INN2_PKGDIR)/S70innd \
		$(TARGET_DIR)/etc/init.d/S70innd
endef

define INN2_LINKS
	ln -sf /etc/news $(TARGET_DIR)/$(INN2_BASEDIR)/etc
	ln -sf /var/spool/news $(TARGET_DIR)/$(INN2_BASEDIR)/spool
	ln -sf /var/log/news $(TARGET_DIR)/$(INN2_BASEDIR)/log
endef

define INN2_CRON
	$(INSTALL) -D -m 0755 $(INN2_PKGDIR)/inn-cron-expire \
		$(TARGET_DIR)/etc/cron.daily
	$(INSTALL) -D -m 0755 $(INN2_PKGDIR)/inn-cron-nntpsend \
		$(TARGET_DIR)/etc/cron.hourly
	$(INSTALL) -D -m 0755 $(INN2_PKGDIR)/inn-cron-rnews \
		$(TARGET_DIR)/etc/cron.hourly
endef

INN2_POST_INSTALL_TARGET_HOOKS += INN2_INSTALL_LINKS
INN2_POST_INSTALL_TARGET_HOOKS += INN2_INSTALL_CRON

define INN2_USERS
	news -1 news -1 ! $(INN2_BASEDIR) /bin/sh -
endef

define INN2_PERMISSIONS
	$(INN2_BASEDIR) d 755 news news - - - - -
	$(INN2_BASEDIR)/bin/innbind f 4510 root news - - - - -
endef

$(eval $(autotools-package))

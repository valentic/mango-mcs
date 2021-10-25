################################################################################
#
# gpsd
#
################################################################################

GPSD_3231_VERSION = 3.23.1
GPSD_3231_SOURCE = gpsd-$(GPSD_3231_VERSION).tar.gz 
GPSD_3231_SITE = http://download-mirror.savannah.gnu.org/releases/gpsd
GPSD_3231_LICENSE = BSD-2-Clause
GPSD_3231_LICENSE_FILES = COPYING
GPSD_3231_CPE_ID_VENDOR = gpsd_project
GPSD_3231_SELINUX_MODULES = gpsd
GPSD_3231_INSTALL_STAGING = YES

GPSD_3231_DEPENDENCIES = host-python3 host-scons host-pkgconf

GPSD_3231_LDFLAGS = $(TARGET_LDFLAGS)
GPSD_3231_CFLAGS = $(TARGET_CFLAGS)
GPSD_3231_CXXFLAGS = $(TARGET_CXXFLAGS)

GPSD_3231_SCONS_ENV = $(TARGET_CONFIGURE_OPTS)

GPSD_3231_SCONS_OPTS = \
	arch=$(ARCH) \
	manbuild=no \
	prefix=/usr \
	sysroot=$(STAGING_DIR) \
	strip=no \
	qt=no \
	systemd=$(if $(BR2_INIT_SYSTEMD),yes,no)

ifeq ($(BR2_PACKAGE_NCURSES),y)
GPSD_3231_DEPENDENCIES += ncurses
else
GPSD_3231_SCONS_OPTS += ncurses=no
endif

# Build libgpsmm if we've got C++
ifeq ($(BR2_INSTALL_LIBSTDCPP),y)
GPSD_3231_LDFLAGS += -lstdc++
GPSD_3231_CFLAGS += -std=gnu++98
GPSD_3231_CXXFLAGS += -std=gnu++98
GPSD_3231_SCONS_OPTS += libgpsmm=yes
else
GPSD_3231_SCONS_OPTS += libgpsmm=no
endif

ifeq ($(BR2_TOOLCHAIN_HAS_GCC_BUG_68485),y)
GPSD_3231_CFLAGS += -O0
GPSD_3231_CXXFLAGS += -O0
endif

# If libusb is available build it before so the package can use it
ifeq ($(BR2_PACKAGE_LIBUSB),y)
GPSD_3231_DEPENDENCIES += libusb
else
GPSD_3231_SCONS_OPTS += usb=no
endif

# If bluetooth is available build it before so the package can use it
ifeq ($(BR2_PACKAGE_BLUEZ5_UTILS),y)
GPSD_3231_DEPENDENCIES += bluez5_utils
else
GPSD_3231_SCONS_OPTS += bluez=no
endif

# If pps-tools is available, build it before so the package can use it
# (HAVE_SYS_TIMEPPS_H).
ifeq ($(BR2_PACKAGE_PPS_TOOLS),y)
GPSD_3231_DEPENDENCIES += pps-tools
endif

ifeq ($(BR2_PACKAGE_DBUS_GLIB),y)
GPSD_3231_SCONS_OPTS += dbus_export=yes
GPSD_3231_DEPENDENCIES += dbus-glib
endif

# Protocol support
ifneq ($(BR2_PACKAGE_GPSD_3231_ASHTECH),y)
GPSD_3231_SCONS_OPTS += ashtech=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_AIVDM),y)
GPSD_3231_SCONS_OPTS += aivdm=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_EARTHMATE),y)
GPSD_3231_SCONS_OPTS += earthmate=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_EVERMORE),y)
GPSD_3231_SCONS_OPTS += evermore=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_FURY),y)
GPSD_3231_SCONS_OPTS += fury=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_FV18),y)
GPSD_3231_SCONS_OPTS += fv18=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_GARMIN),y)
GPSD_3231_SCONS_OPTS += garmin=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_GARMIN_SIMPLE_TXT),y)
GPSD_3231_SCONS_OPTS += garmintxt=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_GEOSTAR),y)
GPSD_3231_SCONS_OPTS += geostar=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_GPSCLOCK),y)
GPSD_3231_SCONS_OPTS += gpsclock=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_GREIS),y)
GPSD_3231_SCONS_OPTS += greis=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_ISYNC),y)
GPSD_3231_SCONS_OPTS += isync=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_ITRAX),y)
GPSD_3231_SCONS_OPTS += itrax=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_MTK3301),y)
GPSD_3231_SCONS_OPTS += mtk3301=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_NMEA),y)
GPSD_3231_SCONS_OPTS += nmea0183=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_NTRIP),y)
GPSD_3231_SCONS_OPTS += ntrip=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_NAVCOM),y)
GPSD_3231_SCONS_OPTS += navcom=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_NMEA2000),y)
GPSD_3231_SCONS_OPTS += nmea2000=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_OCEANSERVER),y)
GPSD_3231_SCONS_OPTS += oceanserver=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_ONCORE),y)
GPSD_3231_SCONS_OPTS += oncore=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_RTCM104V2),y)
GPSD_3231_SCONS_OPTS += rtcm104v2=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_RTCM104V3),y)
GPSD_3231_SCONS_OPTS += rtcm104v3=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_SIRF),y)
GPSD_3231_SCONS_OPTS += sirf=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_SKYTRAQ),y)
GPSD_3231_SCONS_OPTS += skytraq=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_SUPERSTAR2),y)
GPSD_3231_SCONS_OPTS += superstar2=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_TRIMBLE_TSIP),y)
GPSD_3231_SCONS_OPTS += tsip=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_TRIPMATE),y)
GPSD_3231_SCONS_OPTS += tripmate=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_TRUE_NORTH),y)
GPSD_3231_SCONS_OPTS += tnt=no
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_UBX),y)
GPSD_3231_SCONS_OPTS += ublox=no
endif

# Features
ifeq ($(BR2_PACKAGE_GPSD_3231_SQUELCH),y)
GPSD_3231_SCONS_OPTS += squelch=yes
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_OLDSTYLE),y)
GPSD_3231_SCONS_OPTS += oldstyle=no
endif
ifeq ($(BR2_PACKAGE_GPSD_3231_PROFILING),y)
GPSD_3231_SCONS_OPTS += profiling=yes
endif
ifneq ($(BR2_PACKAGE_GPSD_3231_CLIENT_DEBUG),y)
GPSD_3231_SCONS_OPTS += clientdebug=no
endif
ifeq ($(BR2_PACKAGE_GPSD_3231_USER),y)
GPSD_3231_SCONS_OPTS += gpsd_user=$(BR2_PACKAGE_GPSD_3231_USER_VALUE)
endif
ifeq ($(BR2_PACKAGE_GPSD_3231_GROUP),y)
GPSD_3231_SCONS_OPTS += gpsd_group=$(BR2_PACKAGE_GPSD_3231_GROUP_VALUE)
endif
ifeq ($(BR2_PACKAGE_GPSD_3231_MAX_CLIENT),y)
GPSD_3231_SCONS_OPTS += max_clients=$(BR2_PACKAGE_GPSD_3231_MAX_CLIENT_VALUE)
endif
ifeq ($(BR2_PACKAGE_GPSD_3231_MAX_DEV),y)
GPSD_3231_SCONS_OPTS += max_devices=$(BR2_PACKAGE_GPSD_3231_MAX_DEV_VALUE)
endif

ifeq ($(BR2_PACKAGE_PYTHON3),y)
GPSD_3231_SCONS_OPTS += \
	python=yes \
	python_libdir="/usr/lib/python$(PYTHON3_VERSION_MAJOR)/site-packages"
else ifeq ($(BR2_PACKAGE_PYTHON),y)
GPSD_3231_SCONS_OPTS += \
	python=yes \
	python_libdir="/usr/lib/python$(PYTHON_VERSION_MAJOR)/site-packages"
else
GPSD_3231_SCONS_OPTS += python=no
endif

GPSD_3231_SCONS_ENV += \
	LDFLAGS="$(GPSD_3231_LDFLAGS)" \
	CFLAGS="$(GPSD_3231_CFLAGS)" \
	CCFLAGS="$(GPSD_3231_CFLAGS)" \
	CXXFLAGS="$(GPSD_3231_CXXFLAGS)"

define GPSD_3231_BUILD_CMDS
	(cd $(@D); \
		$(GPSD_3231_SCONS_ENV) \
		$(HOST_DIR)/bin/python3 $(SCONS) \
		$(GPSD_3231_SCONS_OPTS))
endef

define GPSD_3231_INSTALL_TARGET_CMDS
	(cd $(@D); \
		$(GPSD_3231_SCONS_ENV) \
		DESTDIR=$(TARGET_DIR) \
		$(HOST_DIR)/bin/python3 $(SCONS) \
		$(GPSD_3231_SCONS_OPTS) \
		$(if $(BR2_PACKAGE_HAS_UDEV),udev-install,install))
endef

define GPSD_3231_INSTALL_INIT_SYSV
	$(INSTALL) -m 0755 -D package/gpsd/S50gpsd $(TARGET_DIR)/etc/init.d/S50gpsd
	$(SED) 's,^DEVICES=.*,DEVICES=$(BR2_PACKAGE_GPSD_3231_DEVICES),' $(TARGET_DIR)/etc/init.d/S50gpsd
endef

# systemd unit files are installed automatically, but need to update the
# /usr/local path references in the provided files to /usr.
define GPSD_3231_INSTALL_INIT_SYSTEMD
	$(SED) 's%/usr/local%/usr%' \
		$(TARGET_DIR)/usr/lib/systemd/system/gpsd.service \
		$(TARGET_DIR)/usr/lib/systemd/system/gpsdctl@.service
endef

define GPSD_3231_INSTALL_STAGING_CMDS
	(cd $(@D); \
		$(GPSD_3231_SCONS_ENV) \
		DESTDIR=$(STAGING_DIR) \
		$(HOST_DIR)/bin/python3 $(SCONS) \
		$(GPSD_3231_SCONS_OPTS) \
		install)
endef

# After the udev rule is installed, make it writable so that this
# package can be re-built/re-installed.
ifeq ($(BR2_PACKAGE_HAS_UDEV),y)
define GPSD_3231_INSTALL_UDEV_RULES
	chmod u+w $(TARGET_DIR)/lib/udev/rules.d/25-gpsd.rules
endef

GPSD_3231_POST_INSTALL_TARGET_HOOKS += GPSD_3231_INSTALL_UDEV_RULES
endif

$(eval $(generic-package))

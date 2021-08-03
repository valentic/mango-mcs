#
# libnfsidmap
#

LIBNFSIDMAP_VERSION = 0.25
LIBNFSIDMAP_SITE = http://www.citi.umich.edu/projects/nfsv4/linux/libnfsidmap
LIBNFSIDMAP_SOURCE = libnfsidmap-$(LIBNFSIDMAP_VERSION).tar.gz
LIBNFSIDMAP_INSTALL_STAGING = YES
LIBNFSIDMAP_LICENSE = BSD-like
LIBNFSIDMAP_LICENSE_FILES = COPYING

$(eval $(autotools-package))

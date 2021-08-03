################################################################################
#
# python2-numpy
#
################################################################################

PYTHON2_NUMPY_VERSION = 1.16.4
PYTHON2_NUMPY_SOURCE = numpy-$(PYTHON2_NUMPY_VERSION).tar.gz
PYTHON2_NUMPY_SITE = https://github.com/numpy/numpy/releases/download/v$(PYTHON2_NUMPY_VERSION)
PYTHON2_NUMPY_LICENSE = BSD-3-Clause, BSD-2-Clause, PSF, Apache-2.0, MIT, Zlib
PYTHON2_NUMPY_LICENSE_FILES = LICENSE.txt doc/sphinxext/LICENSE.txt \
			doc/scipy-sphinx-theme/LICENSE.txt \
			numpy/linalg/lapack_lite/LICENSE.txt \
			tools/npy_tempita/license.txt \
			numpy/core/src/multiarray/dragon4.c
PYTHON2_NUMPY_SETUP_TYPE = setuptools
PYTHON2_NUMPY_DEPENDENCIES = host-python-cython
HOST_PYTHON_NUMPY_DEPENDENCIES = host-python-cython

ifeq ($(BR2_PACKAGE_CLAPACK),y)
PYTHON2_NUMPY_DEPENDENCIES += clapack
else
PYTHON2_NUMPY_ENV += BLAS=None LAPACK=None
endif

PYTHON2_NUMPY_BUILD_OPTS = --fcompiler=None

define PYTHON2_NUMPY_CONFIGURE_CMDS
	-rm -f $(@D)/site.cfg
	echo "[DEFAULT]" >> $(@D)/site.cfg
	echo "library_dirs = $(STAGING_DIR)/usr/lib" >> $(@D)/site.cfg
	echo "include_dirs = $(STAGING_DIR)/usr/include" >> $(@D)/site.cfg
endef

# Some package may include few headers from NumPy, so let's install it
# in the staging area.
PYTHON2_NUMPY_INSTALL_STAGING = YES

$(eval $(python-package))
$(eval $(host-python-package))

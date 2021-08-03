################################################################################
#
# python-gpsd-py3
#
################################################################################

PYTHON_GPSD_PY3_VERSION = 0.3.0
PYTHON_GPSD_PY3_SOURCE = gpsd-py3-$(PYTHON_GPSD_PY3_VERSION).tar.gz
PYTHON_GPSD_PY3_SITE = https://files.pythonhosted.org/packages/62/1e/8ffc5bbbbea1a4501e59280b6f8127d4a54c20888dfa9ffdd7d08a06991a
PYTHON_GPSD_PY3_SETUP_TYPE = setuptools
PYTHON_GPSD_PY3_LICENSE = MIT

$(eval $(python-package))

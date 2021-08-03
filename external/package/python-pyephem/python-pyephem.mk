################################################################################
#
# python-pyephem
#
################################################################################

PYTHON_PYEPHEM_VERSION = 3.7.6.0
PYTHON_PYEPHEM_SOURCE = pyephem-$(PYTHON_PYEPHEM_VERSION).tar.gz
PYTHON_PYEPHEM_SITE = https://files.pythonhosted.org/packages/57/a8/70bb00ea0b71680afdc89779fbbb58d53c9a4da1818d5ef5359b179b46d1
PYTHON_PYEPHEM_SETUP_TYPE = distutils
PYTHON_PYEPHEM_LICENSE = GNU Library or Lesser General Public License (LGPL)
PYTHON_PYEPHEM_LICENSE_FILES = COPYING

$(eval $(python-package))

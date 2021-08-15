################################################################################
#
# mdio-tool 
#
################################################################################

# No releases or tags yet. Use the latest commit ID from main branch
MDIOTOOL_VERSION = 72bd5a915ff046a59ce4303c8de672e77622a86c 
MDIOTOOL_SITE = $(call github,PieVo,mdio-tool,$(MDIOTOOL_VERSION))
MDIOTOOL_LICENSE = GPL-2.0
MDIOTOOL_LICENSE_FILES = LICENSE 

$(eval $(cmake-package))

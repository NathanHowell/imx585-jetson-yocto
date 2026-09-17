IMX585_GIT_URI ?= "git://github.com/NathanHowell/imx585-v4l2-driver.git;protocol=https"
IMX585_GIT_BRANCH ?= "jetson-overlay-6.12.y"

SRC_URI = "${IMX585_GIT_URI};branch=${IMX585_GIT_BRANCH}"
SRCREV = "10660487018e7e46dbc3e493da7ebee1065f429f"

PV = "1.0+git${SRCPV}"

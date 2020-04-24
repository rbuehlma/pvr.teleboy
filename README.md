[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/kodi-pvr/job/pvr.teleboy/job/Matrix/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/rbuehlma%2Fpvr.teleboy/branches/)

# Teleboy PVR addon for Kodi

This is a [Kodi](https://kodi.tv) PVR addon for streaming live TV from [Teleboy](https://www.teleboy.ch).

## Build instructions

1. `git clone --branch master https://github.com/xbmc/xbmc`
2. `git clone --branch Matrix https://github.com/rbuehlma/pvr.teleboy`
3. `cd pvr.teleboy && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.teleboy -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make package-pvr.teleboy`

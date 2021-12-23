# Copyright 2021 Taylor Talkington
# 
# This file is part of OctoPrint-Desktop.
#
# OctoPrint-Desktop is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# OctoPrint-Desktop is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with OctoPrint-Desktop.  If not, see <https://www.gnu.org/licenses/>.
cmake_minimum_required(VERSION 3.16)

project(octoprint-desktop C)

include(FindPkgConfig)

pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(LIBSOUP REQUIRED libsoup-2.4)
pkg_check_modules(JSON_GLIB REQUIRED json-glib-1.0)

set(OPD_SRCS
    src/main.c

    src/app.c
    src/app.h

    src/temp-menu.c
    src/temp-menu.h
    src/psu-menu.c
    src/psu-menu.h
    src/server-menu.c
    src/server-menu.h

    src/config.c
    src/config.h

    src/octoprint/client.h
    src/octoprint/client.c
    src/octoprint/socket.h
    src/octoprint/socket.c
)

add_executable(${CMAKE_PROJECT_NAME} ${OPD_SRCS})
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${GTK3_INCLUDE_DIRS} ${LIBSOUP_INCLUDE_DIRS} ${JSON_GLIB_INCLUDE_DIRS})
target_link_directories(${CMAKE_PROJECT_NAME} PUBLIC ${GTK3_LIBRARY_DIRS} ${LIBSOUP_LIBRARY_DIRS} ${JSON_GLIB_LIBRARY_DIRS})
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC ${GTK3_LIBRARIES} ${LIBSOUP_LIBRARIES} ${JSON_GLIB_LIBRARIES})

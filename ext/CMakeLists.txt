# Olive - Non-Linear Video Editor
# Copyright (C) 2023 Olive Studios LLC
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

add_subdirectory(core EXCLUDE_FROM_ALL)

set(KDDockWidgets_STATIC ON CACHE INTERNAL "Force KDDockWidgets to build statically")
set(KDDockWidgets_QT6 ${BUILD_QT6} CACHE INTERNAL "Conform KDDockWidgets' Qt 6 setting to ours")
# Oak only uses the QtWidgets frontend; building the QtQuick frontend causes
# duplicate QML module registration on macOS and pulls in unused dependencies.
set(KDDockWidgets_FRONTENDS "qtwidgets" CACHE INTERNAL "Only build the QtWidgets frontend for Oak")
add_subdirectory(KDDockWidgets EXCLUDE_FROM_ALL)

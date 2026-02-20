# Olive - Non-Linear Video Editor
# Copyright (C) 2022 Olive Team
# Modifications Copyright (C) 2025 mikesolar
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

add_subdirectory(job)
add_subdirectory(ocioconf)
add_subdirectory(opengl)
add_subdirectory(plugin)
target_sources(libolive-editor PRIVATE
        audioplaybackcache.cpp
        audioplaybackcache.h
        audiowaveformcache.cpp
        audiowaveformcache.h
        cancelatom.h
        colorprocessor.cpp
        colorprocessor.h
        colorprocessorcache.h
        diskmanager.cpp
        diskmanager.h
        framehashcache.cpp
        framememorycache.h
        framememorycache.cpp
        framehashcache.h
        framemanager.cpp
        framemanager.h
        loopmode.h
        managedcolor.cpp
        managedcolor.h
        playbackcache.cpp
        playbackcache.h
        previewaudiodevice.cpp
        previewaudiodevice.h
        previewautocacher.cpp
        previewautocacher.h
        projectcopier.cpp
        projectcopier.h
        renderer.cpp
        renderer.h
        rendercache.h
        renderjobtracker.cpp
        renderjobtracker.h
        rendermanager.cpp
        rendermanager.h
        rendermodes.h
        renderprocessor.cpp
        renderprocessor.h
        renderticket.cpp
        renderticket.h
        shadercode.h
        subtitleparams.cpp
        subtitleparams.h
        texture.cpp
        texture.h
        videoparams.cpp
        videoparams.h
)

set(OLIVE_RESOURCES
        ${OLIVE_RESOURCES}
        PARENT_SCOPE
)

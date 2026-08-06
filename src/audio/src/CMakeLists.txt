# Oak Video Editor - Non-Linear Video Editor
# Copyright (C) 2026 Oak Team
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

add_library(oakaudio SHARED
        audiolevelmeter.cpp
        audiolevelmeter.h
        audiomanager.cpp
        audiomanager.h
        audioprocessor.cpp
        audioprocessor.h
        audiosynchronizer.cpp
        audiosynchronizer.h
        audiovisualwaveform.cpp
        audiovisualwaveform.h
        audiowaveformsync.cpp
        audiowaveformsync.h
        configbridge.cpp
        configbridge.h
        previewaudiodevice.cpp
        previewaudiodevice.h
)

# In a full-tree build the repo root is CMAKE_SOURCE_DIR; a standalone
# build (see src/audio/standalone) sets OAK_REPO_ROOT explicitly.
if(NOT DEFINED OAK_REPO_ROOT)
  set(OAK_REPO_ROOT ${CMAKE_SOURCE_DIR})
endif()

find_package(PortAudio REQUIRED)

target_include_directories(oakaudio PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/core/include
        ${OAK_REPO_ROOT}/ffmpeg_bridge/include
        ${PORTAUDIO_INCLUDE_DIRS}
)

# 01 §1 rule 5: only the OAKAUDIO_API-marked C functions are exported;
# audio-internal C++ classes must not leak into the global symbol
# namespace.
target_compile_options(oakaudio PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
)

# Cross-module access goes through C ABIs only: oakcommon (config,
# ffmpegutils), oakcodec (encoder for recording, decoder for waveform
# extraction), olivecore (Rational/AudioParams/SampleBuffer wrappers),
# ffmpeg_bridge (fb_audio_graph resampler infra, same precedent as
# oakcommon/oakcodec), PortAudio (output device).
target_link_libraries(oakaudio PUBLIC
        oakcommon
        oakcodec
        olivecore
        ffmpeg_bridge
        ${PORTAUDIO_LIBRARIES}
)

# Oak - Non-Linear Video Editor
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

# Post-check for the oak_cli_transcode test: the MP4 exists and ffprobe
# reports an h264 video stream (invoked with -DOUT=<file>).

if (NOT EXISTS "${OUT}")
    message(FATAL_ERROR "transcode output missing: ${OUT}")
endif ()

execute_process(
        COMMAND ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of csv=p=0 "${OUT}"
        OUTPUT_VARIABLE probe_out
        RESULT_VARIABLE probe_rc
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT probe_rc EQUAL 0)
    message(FATAL_ERROR "ffprobe failed on ${OUT}")
endif ()

string(FIND "${probe_out}" "h264" h264_pos)
if (h264_pos EQUAL -1)
    message(FATAL_ERROR "no h264 stream in ${OUT} (ffprobe: ${probe_out})")
endif ()

string(FIND "${probe_out}" "960,540" size_pos)
if (size_pos EQUAL -1)
    message(FATAL_ERROR "unexpected dimensions in ${OUT} (ffprobe: ${probe_out})")
endif ()

message(STATUS "transcode mp4 verified: ${OUT} (${probe_out})")

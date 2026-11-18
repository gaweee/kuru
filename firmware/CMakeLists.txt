cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Explicit version: avoids ESP-IDF's `git describe` (fails on a repo with no commits).
set(PROJECT_VER "0.1.0")
# LVGL tracks dirty rectangles in a fixed buffer (default 32) and falls back to a full-screen
# redraw when it overflows. A knob turn touches ~30 small areas, so give it headroom.
idf_build_set_property(COMPILE_OPTIONS "-DLV_INV_BUF_SIZE=128" APPEND)
project(kuru)

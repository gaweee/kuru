file(GLOB FONT_SRCS "fonts/*.c")
include("${CMAKE_CURRENT_LIST_DIR}/assets/assets.cmake")   # ASSET_SRCS, written by tools/gen_assets.py
idf_component_register(SRCS "ui_screen.c" "ui_status.c" "ui_common.c" "looks.c" ${FONT_SRCS} ${ASSET_SRCS}
                       INCLUDE_DIRS "include" "fonts" "assets"
                       REQUIRES lvgl__lvgl controller)

set(SOURCES main.c app_task.c audio_task.c bt_task.c gcore_task.c gui_task.c pots_task.c)
idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS .
                    REQUIRES audio_drivers bt gcore gui gui_assets i2c lvgl lvgl_tft lvgl_touch spandsp utility)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_INCLUDE_SIMPLE=1)
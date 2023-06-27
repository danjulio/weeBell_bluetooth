file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . 
                       REQUIRES lvgl lvgl_touch lvgl_tft)

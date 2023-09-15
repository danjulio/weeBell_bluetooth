file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../gcore ../../main ../lvgl ../utility
                       REQUIRES lvgl)
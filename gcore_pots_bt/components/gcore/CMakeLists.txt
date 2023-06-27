file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../i2c ../utility ../../main
                       REQUIRES bt app_update bootloader_support)

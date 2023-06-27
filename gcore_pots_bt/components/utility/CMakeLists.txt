file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../audio_assets ../audio_drivers ../../main
                       REQUIRES fatfs)
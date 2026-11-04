cmake_minimum_required(VERSION 3.22)
#cmake_minimum_required(VERSION 3.24) #This is annoying (3.24 is a pain to install...)
project(limrendir C)

set(CMAKE_C_STANDARD 11)

find_package( PkgConfig REQUIRED )

# GTK
pkg_check_modules( GTK REQUIRED gtk+-3.0 )

# GST (GStreamer)
pkg_check_modules(GST REQUIRED gstreamer-1.0
        gstreamer-sdp-1.0
        gstreamer-video-1.0
        gstreamer-app-1.0
        glib-2.0
        gobject-2.0
        )

# ARV (Aravis)
# Aravis encodes its API version in the pkg-config name; accept any supported one (newest first)
pkg_search_module(ARAVIS REQUIRED aravis-0.10 aravis-0.8)

include_directories(.)

# The interface description (limrendir.ui) is embedded in the executable, so that limrendir can be run from any folder
pkg_get_variable(GLIB_COMPILE_RESOURCES gio-2.0 glib_compile_resources)
set(LIMRENDIR_RESOURCES_C ${CMAKE_CURRENT_BINARY_DIR}/limrendir_resources.c)
add_custom_command(
        OUTPUT ${LIMRENDIR_RESOURCES_C}
        COMMAND ${GLIB_COMPILE_RESOURCES} --target=${LIMRENDIR_RESOURCES_C} --generate-source
                --sourcedir=${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/limrendir.gresource.xml
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/limrendir.gresource.xml ${CMAKE_CURRENT_SOURCE_DIR}/limrendir.ui
        COMMENT "Embedding limrendir.ui")

add_executable( limrendir
        logroutines.h
        logroutines.c

        limrendir.h
        limrendir.c
        limrendir_recording.c
        limrendir_camera_settings.c
        limrendir_keyboardcommands.c
        limrendir_streaming.c
        limrendir_fovsettings.c
        limrendir_modechange.c

        main.c limrendir_acquisition_settings.c

        ${LIMRENDIR_RESOURCES_C})

# Compiler warnings (GTK callbacks have fixed signatures, so unused parameters are normal)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(limrendir PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif ()

target_include_directories(limrendir PUBLIC ${GTK_INCLUDE_DIRS} ${GST_INCLUDE_DIRS} ${ARAVIS_INCLUDE_DIRS})
target_link_directories(limrendir PUBLIC ${GSTLIBS_LIBRARY_DIRS} ${ARAVIS_LIBRARY_DIRS})
target_link_libraries(limrendir PUBLIC
        -lm # important pour que des fonctions de maths style log10 marchent. Je ne sais pas pourquoi mais merci StackExchange.
        ${GTK_LIBRARIES}
        ${GST_LIBRARIES}
        ${ARAVIS_LIBRARIES})

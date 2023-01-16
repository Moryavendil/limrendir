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
pkg_check_modules(ARAVIS REQUIRED aravis-0.8)

include_directories(.)

add_executable( limrendir
        logroutines.h
        logroutines.c

        limrendir.h
        limrendir.c
        limrendir_recording.c
        limrendir_acquisitionsettings.c
        limrendir_keyboardcommands.c
        limrendir_streaming.c
        limrendir_fovsettings.c
        limrendir_modechange.c

        main.c)

target_include_directories(limrendir PUBLIC ${GTK_INCLUDE_DIRS} ${GST_INCLUDE_DIRS} ${ARAVIS_INCLUDE_DIRS})
target_link_directories(limrendir PUBLIC ${GSTLIBS_LIBRARY_DIRS} ${ARAVIS_LIBRARY_DIRS})
target_link_libraries(limrendir PUBLIC
        -lm # important pour que des fonctions de maths style log10 marchent. Je ne sais pas pourquoi mais merci StackExchange.
        ${GTK_LIBRARIES}
        ${GST_LIBRARIES}
        ${ARAVIS_LIBRARIES})

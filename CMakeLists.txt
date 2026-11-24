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

# limrendir sources, and the private aravis headers they use (copied from aravis, see UPSTREAM.md)
include_directories(src src/aravis)

# The interface description (limrendir.ui) is embedded in the executable, so that limrendir can be run from any folder
pkg_get_variable(GLIB_COMPILE_RESOURCES gio-2.0 glib_compile_resources)
file(GLOB LIMRENDIR_ICONS ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/scalable/actions/*.svg)
set(LIMRENDIR_RESOURCES_C ${CMAKE_CURRENT_BINARY_DIR}/limrendir_resources.c)
add_custom_command(
        OUTPUT ${LIMRENDIR_RESOURCES_C}
        COMMAND ${GLIB_COMPILE_RESOURCES} --target=${LIMRENDIR_RESOURCES_C} --generate-source
                --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_CURRENT_SOURCE_DIR}/data/limrendir.gresource.xml
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/limrendir.gresource.xml ${CMAKE_CURRENT_SOURCE_DIR}/data/limrendir.ui
                ${LIMRENDIR_ICONS}
        COMMENT "Embedding limrendir.ui")

add_executable( limrendir
        src/logroutines.h
        src/logroutines.c

        src/limrendir.h
        src/limrendir.c
        src/limrendir_recording.c
        src/limrendir_camera_settings.c
        src/limrendir_keyboardcommands.c
        src/limrendir_streaming.c
        src/limrendir_fovsettings.c
        src/limrendir_modechange.c
        src/limrendir_settings.h
        src/limrendir_settings.c
        src/limrendir_overlay.c
        src/limrendir_pixelformat.h
        src/limrendir_pixelformat.c
        src/limrendir_analysis.h
        src/limrendir_analysis.c
        src/limrendir_metrics.h
        src/limrendir_metrics.c
        src/limrendir_metrics_panel.c

        src/main.c src/limrendir_acquisition_settings.c

        ${LIMRENDIR_RESOURCES_C})

# The zebra / focus peaking kernels run on every pixel of the analysed frames: always optimized, even when the rest
# of the program is not (no CMAKE_BUILD_TYPE), so that the analysis thread uses as little CPU as possible
set_source_files_properties(src/limrendir_analysis.c src/limrendir_metrics.c PROPERTIES COMPILE_OPTIONS "-O3")

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

# Tests of the image metrics on synthetic images (not built by default):
#     cmake --build build --target metrics_test && ./build/metrics_test
add_executable(metrics_test EXCLUDE_FROM_ALL
        tests/metrics_test.c
        src/limrendir_metrics.c
        src/limrendir_pixelformat.c)
target_include_directories(metrics_test PRIVATE ${ARAVIS_INCLUDE_DIRS})
target_link_directories(metrics_test PRIVATE ${ARAVIS_LIBRARY_DIRS})
target_link_libraries(metrics_test PRIVATE -lm ${ARAVIS_LIBRARIES})
target_compile_options(metrics_test PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)

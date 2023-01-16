# Limrendir

What is it ?
------------

This software allows to view and save a video stream from a Genicam compatible camera. 
It is a mashup between Aravis Viewer from the [Arvais project](https://aravisproject.github.io/aravis/), which allows for user-friendly interactions with a camera including control and stream visualisation, and [gevCapture](https://gitlab.com/gevcapture/gevcapture), which allows for user-unfriendly recording of said stream.

How to install
--------------

Limrendir is written in C and based on Aravis (0.8), gstreamer (1.0) and GTK+ (3.0).

First you have to install some dependencies to be able to install Aravis

sudo apt install libxml2-dev libglib2.0-dev cmake libusb-1.0-0-dev gobject-introspection \
                 libgtk-3-dev gtk-doc-tools  xsltproc libgstreamer1.0-dev \
                 libgstreamer-plugins-base1.0-dev \
                 libgirepository1.0-dev gettext

Also, because Aravis relies on it you have to install [meson](https://mesonbuild.com/) (if the version given by apt is too old, try using pip) and Cmake (same).

Then [install the Aravis library](https://aravisproject.github.io/aravis/building.html)) (version >= 0.8). 

Then download this project and type 

cmake .
make
./limrendir

Use option --help to know about the available options.


Licence
-------

GNU something.


Why the name ?
--------------
Limrendir is a sindarin (middle-earth elvish) name meaning "the one who swiftly remembers what he looks at". That seemed appropriate for a camera recording software.

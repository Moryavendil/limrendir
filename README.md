# Limrendir


## What is it ?

Limrendir allows to view and save a video stream from a Genicam compatible camera using the [Aravis library](https://aravisproject.github.io/aravis/). 
It is a mashup between Aravis Viewer, which allows for user-friendly interactions with a camera including control and stream visualisation, and [gevCapture](https://gitlab.com/gevcapture/gevcapture), which allows for user-unfriendly recording of said stream.


## How to install

First you have to install some dependencies to be able to install Aravis

    sudo apt install libxml2-dev libglib2.0-dev cmake libusb-1.0-0-dev gobject-introspection \
                     libgtk-3-dev gtk-doc-tools  xsltproc libgstreamer1.0-dev \
                     libgstreamer-plugins-base1.0-dev \
                     libgirepository1.0-dev gettext

Also, because Aravis relies on it you have to install [meson](https://mesonbuild.com/) (if the version given by apt is too old, try using pip).

Then [install the Aravis library](https://aravisproject.github.io/aravis/building.html)) (version >= 0.8). 

Then download this project and in the main directory type

    cmake .
    make
    ./limrendir

Use option `--help` to know about the available options.

Note: If your version of cmake is too old, you can use pip to update it. If it doesn't work, try lowering the required version in the CMakeLists.txt file (but then I can't guarantee everything will work properly).


## I case of missing frames

### Check if the problem comes from the connection to the camera
* Chek that the camera is properly connected to your computer with an appropriate cable (for example, a category 6 Ethernet cable).
* Check that the connection MTU is high enough. Go to network manager, select the network connection, check that MTU >= 8192 (preferrably 9000).

### Check if the problem comes from the permissions given to limrendir
* Run limrendir as administrator (using `sudo`), which will allow to make the Aravis thread relatime.
* Use packet sockets: `sudo setcap cap_net_raw+ep limrendir`
* Nice the program: `sudo nice -5 limrendir`
* Use higher packet timeout and frame retention time: launch limrendir with arguments `-m 1000 -p 1000`

See also the advices [here](https://aravisproject.github.io/aravis/ethernet.html).


## About the code

Limrendir is written in C and based on Aravis (0.8), gstreamer (1.0) and GTK+ (3.0).

A consequent part of the code is directly burrowed or adapted from Aravis Viewer. My additions consist mainly of 
* Smoother control of the acquisition and field of view parameters with key bindings routines inspired by gevCapture and
* Recording capabilities, with different formats supported.

There is a `GObject error` when launching the software. I don't know why. It doesn't seem to inhibit anything so I didn't search too hard. Any patch welcome :)


## Licence

GNU something.


## Why the strange-sounding name ?

Limrendir is a sindarin (middle-earth elvish) name meaning "the one who swiftly remembers what he looks at". That seemed appropriate for a camera recording software ! Many thanks to Elaran for coining the term.

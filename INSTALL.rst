==========================================================================
Embedded Data Transport System Build
==========================================================================

2021-07-21  Todd Valentic
            Initial implementation

--------------------------------------------------------------------------
Introduction
--------------------------------------------------------------------------

This package builds the disk image for a remote data transport sytem. It
uses the buildroot tool to manage the process, starting with building a
toolchain with a compiler and libraries for cross complilation followed
by the Linux kernel and user space tools to create a small but fully
functional Unix system.  On top of this operating system are the
application layers based on the data transport framework. The output
of the build process is a single file that can be installed onto the
embedded system. The goal is to have a repeatable build that requires
no manual setup for the embedded system.

--------------------------------------------------------------------------
Setup
--------------------------------------------------------------------------

This package includes a setup script that will download buildroot, apply
some local patches and configure it for the specific embedded system
image. You will need 15-20GB of free disk space for the entire process.

To start the setup, run::

    ./setup [project]

It will download buildroot into the dl directory and unpack and run
the initial configuration for the project. A default project will be
used if one isn't specificed. Any of the defconfigs can be used. Usually
it will be one of the projects in external/board.

--------------------------------------------------------------------------
Build
--------------------------------------------------------------------------

After setup, you can build the image. To do so, run make in the build
directory::

    cd build/<project>
    make

The process will download the source package for each component (GCC,
Linux kernel, etc), unpack, patch, build and install. At the end, a 
filesystem image will be created in the output directory:: 

    output/images/sdcard.img

The total process take about an hour and will use about 15-20GB of 
disk space. 

--------------------------------------------------------------------------
Installation to SD Card
--------------------------------------------------------------------------

The output image can now be installed on to the target device. For many
systems, this will be an SD card. The image can be copied to the media
with the dd program. Note that the contents of the card will be erased
in this process.

Use lsblk to find the device associated with the SD card. For example,
lsblk shows a 32GB card at /dev/sdd::

    # lsblk

    sdd              8:48   1  29.7G  0 disk 
    ├─sdd1           8:49   1    32M  0 part 
    └─sdd2           8:50   1  29.7G  0 part 

To copy the image to the SD card::

    sudo dd if=output/images/sdcard.img of=/dev/sdd bs=1M status=progress

Make sure you have the right device for the card, otherwise you risk
overwritting one of your other drives! In this example, the card is
at /dev/sdd. You are now ready to boot the image on the embdedded system.

--------------------------------------------------------------------------
Development
--------------------------------------------------------------------------

For further development, you can select new packages to be added to the 
filesystem image, create new packages and modify the file system layout.
Running make will rebuilt the SD card image. The entire build does not
need to be repeated at this point, so it will go much faster than the 
initial build. 


--------------------------------------------------------------------------
References
--------------------------------------------------------------------------

buildroot - Build system for embedded Linux system

    https://buildroot.org

busybox - Provides versions of many common UNIX utilities 

    https://busybox.net




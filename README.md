vohttpd
=======

Simple, Flexible, Fast, Embedded httpd. 

<http://vonger.cn>

##Compile##

###Compile vohttpd###

    $ cd src
    $ make

Then you will get `vohttpd` executable file in current directory.

###Compile Plugin##

    $ cd src
    $ make plugins

`.so` files will be generated in `./plugins`

###Clean###

    $ make clean

###Cross Compile###

    $ cd src
    $ make CROSS_COMPILE=mipsel-openwrt-linux-uclibc-
    $ make plugins CROSS_COMPILE=mipsel-openwrt-linux-uclibc-

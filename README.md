vohttpd
=======

Simple, Flexible, Fast, Embed httpd. 

<http://vonger.cn>

##Compile##

###Compile Httpd###

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
    $ make CROSS_COMPILE=mips-openwrt-linux-uclibc-
    $ make plugins CROSS_COMPILE=mips-openwrt-linux-uclibc-

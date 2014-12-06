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


###TODO List###
  1. move out library control function to a plugin, remove current fixed plugin query html page.
  2. main application should able to load some plugins at start up.
  3. auth plugin.
  4. add https to vohttpd.

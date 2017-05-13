# MyPeb: PHP Erlang Bridge #

This project is a fork of [http://code.google.com/p/mypeb/](http://code.google.com/p/mypeb/). All future development and bug fixes will happen in this github repository.

PEB (Php-Erlang Bridge) is an open-source php extension to run php as an Erlang cnode.

Checkout the wiki for complete manual https://github.com/videlalvaro/mypeb/wiki

## Installation ##

Run the following commands:

```bash
phpize
./configure --help
./configure
make
make install
```

## Note ##

You can specify the location of both Erlang interface libraries and Erlang interface header files by using `--with-erlang=DIR`. The configure script will look for libraries and headers in DIR/lib and DIR/include respectively. In case libraries and headers do not share a common parent directory, use `--with-erlanglib=LIB` and `--with-erlanginc=INCLUDE` instead.

For example with a default installation of Erlang from the `ESL Erlang Packages` on a Mac you can run `configure` like this:

```bash
./configure --with-erlanglib=/usr/local/lib/erlang/lib/erl_interface-3.7.11/lib/ --with-erlanginc=/usr/local/lib/erlang/lib/erl_interface-3.7.11/include/
```
Usage example:

```php
$msg = peb_vencode('[~p,~a]', array( array($link,'getinfo') ) ); //The sender must include a reply address. use ~p to format a link identifier to a valid Erlang pid.

peb_send_byname('pong',$msg,$link);

$message = peb_receive($link); $rs= peb_vdecode( $message) ; print_r($rs);

$serverpid = $rs[0][0];

$message = peb_vencode('[~s]', array( array('how are you') ) ); peb_send_bypid($serverpid,$message,$link); //just demo for how to use peb_send_bypid

peb_close($link); ?> 
```

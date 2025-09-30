#!/bin/sh

case "$1" in
    start)
        echo "Starting aesd daemon code"
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        ;;
    stop)
        echo "Starting aesd daemon code"
        start-stop-daemon -K -n aesdsocket
        ;;
       *)
    exit 1
   esac
exit 0

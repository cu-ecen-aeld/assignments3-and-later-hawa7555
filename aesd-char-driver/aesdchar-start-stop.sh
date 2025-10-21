#!/bin/sh

case "$1" in
    start)
        echo "Loading aesdchar"
        /usr/bin/aesdchar_load
        ;;
    stop)
        echo "Unloading aesdchar"
       /usr/bin/aesdchar_unload
        ;;
       *)
    exit 1
   esac
exit 0


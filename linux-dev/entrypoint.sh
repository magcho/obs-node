#!/bin/sh

Xorg -noreset +extension GLX +extension RANDR +extension RENDER -config /etc/xorg.conf $DISPLAY &
/usr/sbin/sshd -D -e -f /etc/ssh/sshd_config_test_clion &

exec "$@"
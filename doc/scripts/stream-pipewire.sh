#!/bin/bash

# Copyright © 2024 Collabora, Ltd.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial
# portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# Example usage:
# 	stream-pipewire.sh 62 192.168.50.56 5557
#
# Use remoting-client-receive.bash script on the remote side to
# display incoming content

if [ -z $1 -o -z $2 -o -z $3 ]; then
	echo "stream-pipewire.sh pipewire_id host_ip host_port"
	exit 127
fi

pipewire_id=$1
host=$2
port=$3

gst-launch-1.0 rtpbin name=rtpbin ! pipewiresrc path=${pipewire_id} ! videoconvert ! \
	video/x-raw,format=I420 ! jpegenc ! rtpjpegpay ! \
	rtpbin.send_rtp_sink_0 rtpbin.send_rtp_src_0 ! \
	udpsink name=sink host=${host} port=$port rtpbin.send_rtcp_src_0 ! \
	udpsink host=${host} port=$(($port + 1)) sync=false async=false udpsrc port=$(($port + 2)) ! \
	rtpbin.recv_rtcp_sink_0

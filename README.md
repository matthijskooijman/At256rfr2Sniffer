# At256rfr2 sniffer sketch

This repository contains an Arduino sketch that runs on Atmega 256rfr2
microcontrollers and configures on-chip 802.15.4 radio to sniff all
packets and forward them over serial, in either a human readable HEX
format, or a binary pcap format (e.g. as used by Wireshark).

Sniffing happens on channel 20 and bitrate 250kb/s by default, but these
can be configured via serial using e.g. `C11` for channel 11, or `B500`
or 500kb/s. These commands can be newline-terminated, or just stringed
together.

To start sniffing, send either `#` for text-mode sniffing, or `!` for
binary sniffing. If binary sniffing is selected, the sketch sends the
string "SNIF" as a start marker, followed by the raw packets. Each
packet is encoded as a single size byte, followed by that number of
databytes, with no additional framing or escaping.

For example, below is the output from sending `C16B250#` (which is not shown,
since there is no echo on the serial port):

```
802.15.4 packet sniffer starting...                                                                                                                                                                          
 - To configure bitrate, send e.g.: B250                                                                                                                                                                     
   Supported bitrates: 250, 500, 1000, 2000 (default 250 kb/s)                                                                                                                                               
 - To configure channel, send e.g.: C11                                                                                                                                                                      
   Supported channels: 11-26 (default 20)                                                                                                                                                                    
Setting channel: 16                                                                                                                                                                                          
Setting bitrate: 250                                                                                                                                                                                         
Buffer size is 249 packets.                                                                                                                                                                                  
Starting capture with text output                                                                                                                                                                            
418896C6B6FFFF00000912FCFF00001E0DCED26CFEFF10FB3028B7628900CED26CFEFF10FB30005371AC2F959D489D57B66218                                                                                                       
41889AC6B6FFFF00000912FCFF0000010FCED26CFEFF10FB3028BB628900CED26CFEFF10FB300008E704B7AFC44198                                                                                                               
0308E8FFFFFFFF07738A  
```

The binary format is intended to be consumed by
https://github.com/matthijskooijman/serial-pcap, a tool running on the PC that
can configure this sketch and then forward all captured packets to a FIFO to be
consumed by wireshark.

The default settings of tool-serial-pcap should work, but in case they
change, this is the recommended explicit commandline:

```
./serial-pcap  --send-init-delay 2 --send-init 'B250C20!' --read-init 'SNIF' -F /tmp/wireshark /dev/ttyACM0
```

See the [serial-pcap README](https://github.com/matthijskooijman/serial-pcap)
for more details on how to set this up in Wireshark.

## History
The code in this repo was originally written as a module in the default
firmware of the [Pinoccio](https://github.com/Pinoccio/) development
board, based on the atmega256rfr2. Since that board and all development
has halted, this code was extracted into this standalone sketch in 2025.

## License
Files in the lwm directory are taken from the Atmel LWM library and
licensed under the ASF license, see the source files for details.

Unless otherwise indicated, the rest of this repository is licensed
under the BSD license, see license.txt for the full terms.

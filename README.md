# Sub-GHz Transmission with Flipper Zero .sub files and librpitx

This C++ program allows you to read and transmit Flipper Zero sub-GHz files using librpitx on a Raspberry Pi (or other compatible Linux devices). It supports various protocols like RAW, Princeton, and EV1527, and can be extended to support more protocols as needed.

The program reads a Flipper Zero sub-GHz file, parses the contained protocol, frequency, key, and other relevant details, and then transmits it via the specified sub-GHz radio frequency using the Raspberry Pi's GPIO.


## Features

- RAW Data: Transmit raw on-off keying (OOK) data.

- Princeton: Supports Princeton-based protocol for transmitting 1's and 0's.

- Supports a variety of Flipper Zero sub-GHz files.

- Customizable transmission parameters like frequency, duration, and repeat count.

- Dry-run mode to simulate transmission without actually sending data.


## Requirements

- Raspberry Pi (or other compatible Linux-based device with GPIO).

- librpitx installed.

- Flipper Zero sub-GHz file (with protocols like RAW, Princeton, or EV1527).


## Usage

Command-line Options
```
Usage: ./sendsubghz [options] <file.sub>
Options:
  -f freq     Override frequency in Hz (default or from file)
  -r count    Repeat message this many times (default: 1)
  -p pause    Microseconds pause between repeats (default: 1000)
  -d          Dry run (don't transmit)
  -h          Show this help
```
The program return 0 if message send.


### Known limitation

- This program currently supports only static key transmission. Rolling code protocols like Keeloq and NiceFlor are not supported.

- The program is designed to work with Flipper Zero sub-GHz files (.sub). It does not generate these files; you will need to dump them from your Flipper Zero first.


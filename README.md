### Overview

Marvin is a free UCI/XBoard compatible chess engine. It does not provide it's own user interface, instead is is intended to be used with an UCI or XBoard compatible GUI, such as XBoard/WinBoard, Arena, HIARCS Chess Explorer, Shredder or Fritz. For instructions on how to install the engine see the documentation for the respective GUI.

### History

Marvin was originally released as closed source in 2002 and activly developed until 2005. The last release was 1.3.0. In 2015 the development was restarted and a completely rewritten version was released as open source in 2017.

### Configuration

When started Marvin looks for a configuration file called marvin.ini in the same directory as the excutable. This file can be used to configure the engine. Currently the following options are recognized:
* HASH_SIZE: The amount of memory used for the main hash table (in MB).
* LOG_LEVEL: The log level. If set to 2 the engine will log all commands that are sent and received.

Additionally Marvin looks for a file called book.bin in the same directory. The book.bin file should be an opening book file in Polyglot format.

### Building

The easiest way to build Marvin is to use GCC and the included Makefile. Running `make` should produce a binary that is comaptible with your system. For more information about availbale targets and options run `make help`.

### License

The source code is provided under the GPL3 license. For details see the LICENSE file.

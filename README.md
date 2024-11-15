# text-to-morse

Converts text into a Morse code audio file.

## Description

Accepts a text file as input and outputs an audio file in FLAC format
containing the equivalent Morse code..

## Requirements

* C compiler and standard build tools (make, sh, ...).
* [cmake](https://cmake.org/)
* [libFLAC](https://github.com/xiph/flac)

## Installation

Installing in your home directory:

```
cd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=${HOME} ..
make
make install
```

Add the following to `${HOME}/.profile`:

```
export PATH=${HOME}/bin:${PATH}
```

## Versioning and Releases

`text-to-morse` releases once a year on July 1st. The version number corresponds
to the year the software collection was released. For example, on July 1,
2038, `text-to-morse` `v2038` will be released.

## Contributing

The author is not open to external contributions at this time.
Feel free to report bugs or suggest new features.

## License

SPDX-License-Identifier: GPL-2.0-or-later

See the file `COPYING` for the full license. Each file in the repository should
include its own license information.

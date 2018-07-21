# PatchMatrix

## a JACK patchbay in flow matrix style

PatchMatrix gives the best user experience with JACK1, as it makes intensive use of
JACK's metadata API, which JACK2 still lacks an implementation of.

### Build status

[![build status](https://gitlab.com/OpenMusicKontrollers/patchmatrix/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/patchmatrix/commits/master)

### Screenshot
![Screenshot](https://gitlab.com/OpenMusicKontrollers/patchmatrix/raw/master/patchmatrix_screeny.png "PatchMatrix Screenshot")

### Mouse actions

#### Canvas

* Middle button + move: _move canvas_
* Right button: _open context menu_

#### Client

* Left button + Ctrl + move: _move client_

#### Grab handle

* Left button: _connect clients w/o connecting ports within_
* Left button + Ctrl: _connect clients and ports automagically_

#### Mixer

* Left button + move: _change gain coarse_
* Wheel: _change gain coarse_
* Left button + Shift + move: _change gain fine_
* Wheel + Shift: _change gain fine_
* Right button: _remove_

#### Monitor

* Rigth button: _remove_

#### Matrix

* Left button: _toggle port connection_
* Left button + Ctrl + move: _move matrix_
* Wheel: _toggle port connection_
* Right button: _remove and disconnect all ports_

### Automation

#### MIDI

PatchMatrix mixer clients (AUDIO + MIDI) each have an additional JACK MIDI
automation port through which users can automate mixer matrix gains sample-accurately.

Currently, users have to send multiple MIDI messages for a single gain change
in a stateful transactional manner.

* NRPN-LSB: set index of sink port column (optional)
* NRPN-MSB: set index of source port row (optional)
* DATA-LSB: set lower 7 bits of gain (optional)
* DATA-MSB: set higher 7 bits of gain (mandatory)

DATA-MSB finalizes one transaction and sets gain to new value for currently
set sink/source port indexes.

### Binaries

Extract matching platform subdirectory into _/opt_ and start with _/opt/patchmatrix/bin/patchmatrix_

#### Stable

https://dl.open-music-kontrollers.ch/patchmatrix/stable/patchmatrix-latest-stable.zip

#### Unstable / Nightly

https://dl.open-music-kontrollers.ch/patchmatrix/unstable/patchmatrix-latest-unstable.zip

### Dependencies

#### Runtime
* [JACK](http://jackaudio.org/) (JACK audio connection kit)

#### Buildtime
* [LV2](http://lv2plug.in) (LV2 Plugin Specification)

### Build / install

	git clone https://gitlab.com/OpenMusicKontrollers/patchmatrix.git
	cd patchmatrix 
	meson build
	cd build
	ninja -j4
	sudo ninja install

### License

Copyright (c) 2016-2018 Hanspeter Portner (dev@open-music-kontrollers.ch)

This is free software: you can redistribute it and/or modify
it under the terms of the Artistic License 2.0 as published by
The Perl Foundation.

This source is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
Artistic License 2.0 for more details.

You should have received a copy of the Artistic License 2.0
along the source as a COPYING file. If not, obtain it from
<http://www.perlfoundation.org/artistic_license_2_0>.

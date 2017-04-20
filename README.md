# PatchMatrix

## a JACK patchbay in flow matrix style

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

### Dependencies

#### Runtime
* [JACK](http://jackaudio.org/) (JACK audio connection kit)

#### Buildtime
* [LV2](http://lv2plug.in) (LV2 Plugin Specification)

### Build / install

	git clone https://gitlab.com/OpenMusicKontrollers/patchmatrix.git
	cd patchmatrix 
	mkdir build
	cd build
	cmake ..
	make
	sudo make install

### License

Copyright (c) 2016-2017 Hanspeter Portner (dev@open-music-kontrollers.ch)

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

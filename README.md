# PatchMatrix

## a JACK patchbay in flow matrix style

### Build status

[![build status](https://gitlab.com/OpenMusicKontrollers/patchmatrix/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/patchmatrix/commits/master)

### Screenshot
![Screenshot](https://gitlab.com/OpenMusicKontrollers/patchmatrix/raw/master/patchmatrix_screeny.png "PatchMatrix Screenshot")

### Dependencies

* [JACK](http://jackaudio.org/) (JACK audio connection kit)
* [LV2](http://lv2plug.in) (LV2 Plugin Specification)

### Build / install

	git clone https://github.com/OpenMusicKontrollers/patchmatrix.git
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

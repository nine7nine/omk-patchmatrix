## PatchMatrix

### a JACK patchbay in flow matrix style

A simple graphical JACK patchbay that tries to unite the best of both worlds:

* Fast patching and uncluttered port representation of a **matrix patchbay**
* Excellent representation of signal flow of a **flow canvas patchbay**

It additionally features tightly embedded graphical mixer clients automatable
with JACK MIDI/OSC.

### Packages

* [ArchLinux](https://www.archlinux.org/packages/community/x86_64/patchmatrix/)

#### Mouse actions

##### Canvas

* Middle button + move: _move canvas_
* Right button: _open context menu_

##### Client

* Left button + Ctrl + move: _move client_

##### Grab handle

* Left button: _connect clients w/o connecting ports within_
* Left button + Ctrl: _connect clients and ports automagically_

##### Mixer

* Left button + move: _change gain coarse_
* Wheel: _change gain coarse_
* Left button + Shift + move: _change gain fine_
* Wheel + Shift: _change gain fine_
* Right button + Ctrl: _remove_

##### Monitor

* Rigth button: _remove_

##### Matrix

* Left button: _toggle port connection_
* Left button + Ctrl + move: _move matrix_
* Wheel: _toggle port connection_
* Right button: _remove and disconnect all ports_

#### Automation

##### MIDI

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

##### OSC

PatchMatrix mixer clients (AUDIO + MIDI) additionaly support JACK OSC
automation through which users can automate mixer matrix gains sample-accurately.

    /patchmatrix/mixer iif (source index) (sink index) (gain in mBFS [-3600,3600])

#### Dependencies

##### Runtime

* [JACK](http://jackaudio.org/) (JACK audio connection kit)

##### Buildtime

* [LV2](http://lv2plug.in) (LV2 Plugin Specification)

#### Build / install

	git clone https://git.open-music-kontrollers.ch.ch/~hp/patchmatrix
	cd patchmatrix 
	meson build
	cd build
	ninja -j4
	sudo ninja install


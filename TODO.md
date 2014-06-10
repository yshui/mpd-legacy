# Upstream changes to pull in:
* Remove whence from seek().

# (Possibly) undocumented feature:
* rename command.

# Learn the code:
* Read the filter plugin code.

# Easy Tasks:
* Refine db plugin api.
* Checksumming, add a unique id to songs.
* Implement crossfade using filter plugin api
* more accurate idle event.
* reporting database update progress

# Thoughts on this branch:
* the playlist api has too low abstraction, there're many duplicated code for reading a stream.
* Rename the route filter to remap filter.
* pcm_volume.c should go into utils.
* Need a function to convert errno to MPD_ERROR.
* Extract parsing of common encoder param, reduce code dup.

# DB plugin API:
* After I made the db change, too much internals have been exposed to db plugin (maybe that's just fine)
* Before that change it's (practically) impossible to write a different db plugin.

# Long Term Goals:
* Loadable plugins.
* DBus support.
* Mutliple control protocol (like json over http)
* Create outputs on the fly (like http streams)
* Get rid of GLib in core mpd. glib in plugins in fine since I gonna implement loadable plugin support.

# Possible alternatives to GLib functionalities
* Multithread -> Single thread (lots of works to be done)
* Async io -> libeio
* Event loop -> libev
* Other things -> we'll see
* Need a hash table implementation. (khash.h ?)
* Need Log facility (write my own ?)
* UTF8 (use icu, and utf16)

# Rants
* Don't split source files into less-than-two-hundred-line pieces, unless you have a VERY GOOD reason. It's nonsense.

# Competitiveness
* There are so many good music players out there, see xmms2, albertz's music player, mopidy, etc. (mopidy even supports mpd protocol). If you want to server files over network, there's also shiva. So why would anyone even using mpd?


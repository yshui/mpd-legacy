# Learn the code:
* Read the filter plugin code.

# Easy Tasks:
* Refine db plugin api.
* Checksumming, add a unique id to songs.
* Implement crossfade using filter plugin api
* more accurate idle event.
* reporting database update progress

# DB plugin API:
* After I made the db change, too much internals have been exposed to db plugin (maybe that's just fine)
* Before that change it's (practically) impossible to write a different db plugin.

# Long Term Goals:
* (Possibly) get rid of GLib.
* Loadable plugins.
* DBus support.

# Possible alternatives to GLib functionalities
* Multithread -> Single thread (lots of works to be done)
* Async io -> libeio
* Event loop -> libev
* Other things -> we'll see

# Rants
* Don't split source files into less-than-two-hundred-line pieces, unless you have a VERY GOOD reason. It's nonsense.

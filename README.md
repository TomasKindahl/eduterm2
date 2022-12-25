# Eduterm/2

This is a clone of [vain's][vain] ([Peter Hofmann's][phoff]) [eduterm], used by myself to develop terminal functionality in [kråkfot][krakfot]. To honor vain's contribution, I keep his MIT license. But don't nag on him to fix errors in my clone!

My plan is to redevelop eduterm/2 to use a real text line buffer (in opposition to a height/width square letter drawing area matrix), as well as modularize it rather than writing naked bytes to allocated memory.

[eduterm]: https://www.uninformativ.de/git/eduterm/file/README.html
[vain]: https://github.com/vain
[krakfot]: https://github.com/TomasKindahl/krakfot
[phoff]: https://www.uninformativ.de/contact.html

## Documentation from uninformativ.de
<pre>
    ___  __| |_   _| |_ ___ _ __ _ __ ___
   / _ \/ _` | | | | __/ _ \ '__| '_ ` _ \
  |  __/ (_| | |_| | ||  __/ |  | | | | | |
   \___|\__,_|\__,_|\__\___|_|  |_| |_| |_|
</pre>

> <code><a href="https://uninformativ.de/git/eduterm">https://uninformativ.de/git/eduterm</a></code> (dead link!)<br/>
> <code><a href="https://uninformativ.de/bugs.html">https://uninformativ.de/bugs.html</a></code>

----

Learning what's going on behind the scenes in X11 terminal emulators.

This is for educational purposes.


## Installation

The following C libraries are required:

- libx11

To build the program, run:

    make

You really should read the source code.

## Running

Open a new terminal window:

    eduterm

If you use a tiling window manager, make sure that you're in "floating" mode.

## Mayday
**WIP**\
This is a wayland compositor that exists to serve WMs. Think of it as the XServer, but for wayland. For the small fry, by the small fry.

## Architecture
Every (most) objects have an associated ObjectData struct, that is their user_data. For ObjectData's that are structs, that means all their fields are meant to be directly assigned.\
For those that are classes, it means they have a constructor. 

##  Licensing
The project's source code is licensed under `LGPL-3.0-or-later`.

The branding (eg. project name, logos etc.) is not covered by the aforementioned license. Reasonable descriptive use (eg. packaging, articles, etc.) is completely fine.

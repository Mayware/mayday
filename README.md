## Mayday
**WIP**\
This is a wayland compositor that exists to serve WMs. Think of it as the XServer, but for wayland. For the small fry, by the small fry.

## Scripts
`gen.sh` re-runs mayquill's generator
`mkbuild.sh` generates the build directories for mayday. It also runs `gen.sh`
`build.sh` builds mayday
`execute.sh` executes mayday
`run.sh` runs both `build.sh` and `execute.sh`

## Architecture
Every (most) objects have an associated ObjectData struct, that is their user_data. For ObjectData's that are structs, that means all their fields are meant to be directly assigned.\
For those that are classes, it means they have a constructor. 

The "applied" / real surface state is ALWAYS suitable for the renderer to read. All committed deltas which aren't ready yet because of acquire points, shm/dmabuf uploads not complete,
FIFO barrier not yet triggered, are prevented from being applied in the first place. Committed deltas are only ever applied once all their constraints are met, so the renderer has a
low cortisol experience, and the shit makers deal with their own shit. `surfaces.cppm` is your goto for the main DCU/SCU/committing deltas logic.

##  Licensing
The project's source code is licensed under `LGPL-3.0-or-later`.

The branding (eg. project name, logos etc.) is not covered by the aforementioned license and remains the sole property of `kingdomkind`. Reasonable descriptive use (eg. packaging, articles, etc.) is completely fine.

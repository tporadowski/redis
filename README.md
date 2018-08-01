### Redis 4.0.2 for Windows - alpha release!

You can find the first alpha release of Redis 4.0.2 for Windows on [releases page](https://github.com/tporadowski/redis/releases). Please test it and report any issues, thanks in advance!

**DISCLAIMER**

At the moment this is a **highly experimental port of [Redis 4.0.2](https://github.com/antirez/redis/releases/tag/4.0.2) for Windows x64** merged with archived port of [win-3.2.100 version](https://github.com/MicrosoftArchive/redis/releases/tag/win-3.2.100) from MS Open Tech team. Since the latter is no longer maintained - I merged the sources by hand, updated projects to Visual Stydio 2017 (v15.4.1) and applied whatever fast fixes I could figure of to make it buildable. Some things are still not working yet (modules), but since this is the very first time I've seen this code (not to mention that I haven't used C in a while) - I have no idea yet how it all works internally :). Nevertheless, it compiles, runs and can be further adjusted to comply with full-featured Redis 4.0.2.

You can find the original description of what this fork provides, how it evolved, what are its requirements, etc. on Wiki: https://github.com/tporadowski/redis/wiki/Old-MSOpenTech-redis-README.md

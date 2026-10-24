# Documentation (from nfsmw-nx)

These pages come from [nfsmw-nx](https://github.com/StevensND/nfsmw-nx), the Need for Speed: Most Wanted port this
repository is forked from. They describe the parts that are shared with this port: the toolchain, building the SDK
and Mesa (NVK), the Horizon platform layer, the profiler and porting another game.

Places that mention `app/`, `shaders/`, `pgo/`, the installer page or NFS:MW editions refer to nfsmw-nx; those parts
are not in this repository. For Call of Duty Classic see [../README.md](../README.md) and
[../codclassic/BUILDING.ru.md](../codclassic/BUILDING.ru.md).

| Page | Contents |
| --- | --- |
| [toolchain.md](toolchain.md) | devkitA64, libnx and the host tools |
| [building.md](building.md) | Building the SDK, the code generator and the NRO |
| [mesa.md](mesa.md) | Building NVK from mesa-switch with the patch in `mesa/` |
| [platform-notes.md](platform-notes.md) | Horizon specifics of the runtime (memory, threads, exceptions) |
| [measuring.md](measuring.md) | The built-in profiler (`rex_perfil.log`) |
| [porting-another-game.md](porting-another-game.md) | Using this runtime for another Xbox 360 game |
| [glossary.md](glossary.md) | Terms |

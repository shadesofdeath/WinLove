# Win11Debloat — empty Start state

`start2-empty.bin` (972 bytes) is `Assets/Start/start2.bin` of [Raphire/Win11Debloat](https://github.com/Raphire/Win11Debloat)
(MIT, see `LICENSE`; blob 98159fb0e65f33de40f1ddfc545c103385565767): a Windows 11 Start state with no pinned apps.
WinLove embeds it (`src/core/image/StartMenu.cpp`, Base64) and writes it into the image's default profile for the
"Boş" Start (D-069). Kept here as the source of that constant.

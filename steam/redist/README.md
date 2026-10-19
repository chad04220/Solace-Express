# steam/redist

Put Valve's `steam_api64.dll` here: from the Steamworks SDK (partner.steamgames.com > Downloads), the file
`sdk/redistributable_bin/win64/steam_api64.dll`. Any SDK from 1.53 on (2021) works: the game finds the interface
versions the DLL has at run time (`src/steam.cpp`).

The Steamworks SDK's terms let the DLL ship with the game on Steam, not sit in a public repository, so `.gitignore`
keeps it out for now. Once the repository is private, delete the `steam/redist/*.dll` line from `.gitignore` and
commit the DLL: the CI's `steam` job copies it beside the exe in the Steam depot (only there - the GitHub zip never
carries it).

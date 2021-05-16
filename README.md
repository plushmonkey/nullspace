# nullspace
SubSpace client that I will probably never work on.  

## Features
- VIE and Continuum encryption
- Download map and LVZ data
- Chat
- Render statbox
- Render radar with some indicators
- Render tiles and animated tiles
- Render simple LVZ objects (map and some screen)
- Render players
- Render simple weapons like bullets and bombs
- Player/weapon-tile collision/bounce
- Player-weapon collision - bombs not exactly correct yet
- Spectator camera
- Entering and controlling a ship
- Energy and recharge
- Shoot bullets/bombs with correct weapon data
- Initial bounty prizing with correct prizing
- Item indicators
- Cloak, stealth, xradar, antiwarp, and multifire
- Door synchronization
- Android spectator client (only tested in emulator)

## Running
Follow the security requirements listed below and drop the built nullspace.exe into Continuum folder.  

## Security

### Key expansion
The key expansion generator is not included for game integrity reasons.  
The client can still be run on servers you have access to by putting the same scrty1 file in the nullspace folder that exists on the server. May not work if the server uses connected clients to update their scrty1 file with key expansion challenges.   

### Memory
The security executable checksum challenge requires the dumped Continuum memory.  
I'm not including it in this repository but the instructions for dumping are listed below.  

1. Download [petools](https://github.com/petoolse/petools/releases)
2. Run Continuum.exe (leave at menu and don't do anything)
3. Run petools as administrator (Right click, Run as administrator)
4. Right click Continuum in the list in petools, click Dump Region
5. Click address 0x401000 and click dump, save it as cont_mem_text
6. Click address 0x4A7000 and click dump, save it as cont_mem_data
7. Put the two files in the nullspace folder

## Building

### Windows
Use the existing Visual Studio solution or install cmake.

### Linux
1. Install GLFW3 development libraries (`sudo apt-get install libglfw3-dev` on Ubuntu)
2. Install cmake
3. Open terminal in nullspace directory.
4. `mkdir build && cd build`
5. `cmake ..`
6. `make`

### Android
Use the provided gradlew in the android folder.

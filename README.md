# nullspace
SubSpace client that I will probably never finish.  

## Running
Follow the security requirements listed below and drop the built nullspace.exe into Continuum folder.  
Tested on modern Windows 10 and Linux setup, 12 year old Windows 10 laptop, and Android emulator.  

### Video
Example video showcasing the client in Hyperspace, MetalGear, and a local server with Trench Wars settings.  
The actual client is much smoother than the video shows.  

[![Example video](https://i.imgur.com/dIGWkfP.png)](http://www.youtube.com/watch?v=VhohJr5V_tQ "Subspace Continuum - nullspace client")  

## Features
- Windows, Linux, and Android (spectator client tested in emulator)
- VIE and Continuum encryption
- Download map and LVZ data
- Statbox, radar, chat, energy, item/ship status indicators, and menu ui elements
- Render tiles and animated tiles
- Render simple LVZ objects (map and some screen)
- Render players and weapons
- Player/weapon collision/bounce
- Spectator camera
- Entering and controlling a ship with correct arena settings
- Render exhaust
- Energy and recharge
- Bullets, bouncing bullets, bombs, proximity bombs, mines, repels, and shrap implemented with correct simulation
- Bursts, repels, decoys, thors, bricks, rockets, and portals implemented
- Afterburners
- Initial bounty prizing with correct prizing
- Cloak, stealth, xradar, antiwarp, and multifire
- Super and shields
- Door synchronization
- Green synchronization and prize weighting
- Banners
- Attaching
- Notifications in center of screen
- Sound effects
- Flag pickup and flag turf claiming

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

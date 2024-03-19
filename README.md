# nullspace
SubSpace client that I will probably never finish.  

## Running
The client is designed to be a drop-in replacement client for Continuum. Build the client and drop it into the Continuum folder.  
Tested on modern Windows 10 and Linux setup, 12 year old Windows 10 laptop, and Android phone.  

A built VIE-only client for Windows can be downloaded in the [Release](https://github.com/plushmonkey/nullspace/releases) section.  
The local server is set to `127.0.0.1:5000` and subgame is set to `127.0.0.1:5002`. You need to compile your own version to have these changed.  

#### Security
Solving the checksum and key expansion requests is done by using a private network service. There's no public server available yet.  
The security service needs to remain private to maintain integrity of the game. It would be too easy to cheat / attack servers if it was released.  

This means that the client cannot connect to any Continuum-only zones by default.  
The zones will need to have VIE encryption enabled. Check the documentation of the server to learn how to enable this.  

### Video
Example video showcasing the client in Hyperspace, Extreme Games, and a local server with Trench Wars settings.  

[![Example video](https://i.imgur.com/tqERkM1.png)](https://www.youtube.com/watch?v=AOaTF5v-xW4 "Subspace Continuum - nullspace client 2")  

## Features
- Windows, Linux, and Android (Android spectator client currently)
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
- Balls

## Building
### Getting source
1. `git clone https://github.com/plushmonkey/nullspace`
2. `cd nullspace`
3. `git submodule init && git submodule update`

Most of the client configuration can be done in `main.cpp`. The `InitialSettings` function and the `kServers` array are the main sections that might need to be changed.

### Windows
Use the existing Visual Studio solution or install cmake. Choose Release x64 build.

### Linux
1. Install GLFW3 development libraries (`sudo apt-get install libglfw3-dev` on Ubuntu)
2. Install cmake
3. Open terminal in nullspace directory.
4. `mkdir build && cd build`
5. `cmake ..`
6. `make`

### Android
Use the provided gradlew in the android folder.  
This version doesn't support input and isn't really maintained, so it might not compile.
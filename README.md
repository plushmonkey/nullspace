# nullspace
SubSpace client that I will probably never finish.  

## Running
The client is designed to be a drop-in replacement client for Continuum. Build the client and drop it into the Continuum folder.  
Tested on modern Windows 10 and Linux setup, 12 year old Windows 10 laptop, and Android emulator.  

### Video
Example video showcasing the client in Hyperspace, MetalGear, and a local server with Trench Wars settings.  
The actual client is much smoother than the video shows.  

[![Example video](https://i.imgur.com/dIGWkfP.png)](http://www.youtube.com/watch?v=VhohJr5V_tQ "Subspace Continuum - nullspace client")  

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

## Security
Solving the checksum and key expansion requests is done by using a private network service. There's no public server available yet.

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

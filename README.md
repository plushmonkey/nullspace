# nullspace
SubSpace client that I will probably never work on.  
It can connect to Continuum encrypted zones, download map data, and perform security challenges.  

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

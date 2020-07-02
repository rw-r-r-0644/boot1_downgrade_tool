# boot1_downgrade_tool
### DO NOT USE THIS TOOL UNLESS YOU KNOW EXACTLY WHAT YOU ARE DOING!  
### IT IS ALMOST GUARANTEED THAT YOU WILL BRICK, AND EVEN AN SLC HARDMOD MIGHT NOT SAVE YOU!  
    
This program will install the provided boot1 version on your console.  
  
##### Compiling
First, edit `castify.py` to add the Starbuck key/IV - this is required to work with current CFW loaders. Then, just run make. You'll need devkitPPC, devkitARM, and armips - you should have all these if you've compiled many CFWs before; I won't go into it here. You should end up with a fw.img, along with a bunch of ELF files.

### Setup
You'll need an SD card normally readable by the Wii U. Simply copy the fw.img to the root of the SD.
Copy the encrypted boot1 image (extracted from the boot1 NUS title) to the root of your SD as boot1.target

### Running
Once you've done everything in the Setup stage, just run your favorite CFW booter (CBHC works, as does the old wiiubru loader). With any luck, boot1-downgrade-tool will start the downgrade process.

### Credits
- This tool is based on [linux-loader](https://gitlab.com/linux-wiiu/linux-loader) and only applies minor changes  
- linux-loader is based on [minute](https://github.com/Dazzozo/minute)  

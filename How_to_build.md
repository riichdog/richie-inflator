# How to build  

Following guide is based on VSTSDK v3.7.12, where Windows bundle build is fixed and AudioUnit SDK is supported.  
VSTSDK v3.8.1 is also tested (with Xcode 26). v3.7.12 fails to compile with that compiler.  

## Quick start (command line, macOS)

``` console
git clone --branch v3.8.1_build_84 --recursive https://github.com/steinbergmedia/vst3sdk.git ~/VST_SDK/vst3sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF
cmake --build build
```

The SDK path defaults to `~/VST_SDK/vst3sdk` on macOS and `C:/VST_SDK/vst3sdk` on Windows. Override it with `-Dvst3sdk_SOURCE_DIR=/path/to/vst3sdk`.  
If `xcode-select` points at the Command Line Tools, prefix the commands with `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`.  

### UI tools

Configure with `-DJSIF_BUILD_UI_TOOLS=ON` to also build:

- `jsif_ui_tests` and `jsif_controller_tests`: unit tests for the Modern editor views, skin state and value readouts.
- `jsif_snapshot`: renders an editor template to PNG without a host, for example:  
  `build/bin/Debug/jsif_snapshot build/VST3/Debug/JS_Inflator.vst3 Modern out.png 0.5 0=0.5 t:0=0.00`  
  `tag=value` sets a control's normalized value, `t:tag=text` sets a readout's text and `m:tag=text` sets a menu entry.

## 0. Set VSTSDK  

Download or clone VSTSDK and place it where you're least likely to move/rename/etc.  

### macOS - Check AudioUnit SDK Path  

To build AUv2 plugin, you need official AudioUnit SDK and the SMTG_AUDIOUNIT_SDK_PATH must be set.  
Repo : [https://github.com/apple/AudioUnitSDK](https://github.com/apple/AudioUnitSDK)  

Clone the repo right next to vst3sdk folder so it looks like this;  

![Clone this repo using VS Code](screenshots/Guide/0-1.png)  

## 1. Clone this repo

![Clone this repo using VS Code](screenshots/Guide/1-1.png)  
![Clone this repo using VS Code](screenshots/Guide/1-2.png)  

Use any method you like. I used VS Code.  

## 2. Recursivly clone submodules

![Clone this repo using VS Code](screenshots/Guide/2-1.png)  

Clone all submodules in this repo.  
Use following command in Terminal in VS Code.  

``` git
git submodule init
git submodule update
```

## 3. Configure CMake

![Clone this repo using VS Code](screenshots/Guide/3-1.png)  

Set source code directory.  

![Clone this repo using VS Code](screenshots/Guide/3-2.png)  

Set build directory. It doesn't exiest by default, so make one.  

![Clone this repo using VS Code](screenshots/Guide/3-3.png)  
![Clone this repo using VS Code](screenshots/Guide/3-4.png)  

Add CMake entry by your OS - SMTG_MAC / SMTG_WIN - and set it true.  

![Clone this repo using VS Code](screenshots/Guide/3-5.png)  

Start cofigure and set IDE you like.  

![Clone this repo using VS Code](screenshots/Guide/3-6.png)  

Now, all SMTG entries appeared.  

![Clone this repo using VS Code](screenshots/Guide/3-7.png)  
![Clone this repo using VS Code](screenshots/Guide/3-8.png)  

Turn OFF these entries, as we don't use them here;

- SMTG_ENABLE_VST3_PLUGIN_EXAMPLES
- SMTG_ENABLE_VST3_HOSTING_EXAMPLES
- SMTG_MDA_VST3_VST2_COMPATIBLE

Click configure again to be sure.  

### Windows - Build plugin as file, not folder  

Turn OFF next entry to build as file;  

- SMTG_CREATE_BUNDLE_FOR_WINDOWS

When turned on, it will create plugin as 'bundle', which is standard in macOS and Linux.  
BUT, I still prefer single file plugin.  

## 4. Generate and Open Peoject  

![Clone this repo using VS Code](screenshots/Guide/4-1.png)  

Done!

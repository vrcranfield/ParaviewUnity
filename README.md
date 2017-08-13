# How to successfully build and install a ParaUnity

## Building
*As seen [here](https://github.com/RCBiczok/ParaUnity).*

### Prerequisites
  * CMake 3.8.1
  * Visual Studio 2015 x64 Community Edition

### Compile Qt 4.8.6
*Note:* if you have troubles with the compilation, have a look [here](https://stackoverflow.com/questions/32848962/how-to-build-qt-4-8-6-with-visual-studio-2015-without-official-support) or [here](https://forum.qt.io/topic/56453/compiling-qt4-head-with-msvc-2015-cstdint-errors/11).

The files in `/Qt 4.8.6` are a patched version of Qt (see previous links; I used python's `patch.py` and the `.diff` file in that folder) that lets you compile it with Visual Studio 2015 x64.
In order to build it do the following:
  * Move the content of `Qt 4.8.6` in `C:\Qt\4.8.6`.
  * Open the `VS2015 x64 Native Tools Command Prompt` from Start
  * `cd C:\Qt\4.8.6`
  * `./configure.exe -make nmake -platform win32-msvc2015 -prefix c:\Qt\4.8.6 -opensource -confirm-license -nomake examples -nomake tests -nomake demos -debug-and-release`
  * `nmake`
  * `nmake install`
  * Add `C:\Qt\4.8.6\bin` to `Path`

### Compile ParaView 5.2.0
I decided to use version 5.2.0 because it was the latest that supported Qt4 by default. It could probably work with the latest as well, but I wouldn't count on that.
In order to build it do the following:

  * Open CMake with sources in `\ParaView-v5.2.0` and build in `\ParaView-v5.2.0\build`
  * Configure with `Visual Studio 14 2015 Win64` as a generator
  * Check that `PARAVIEW_QT_VERSION` is `4` and that `QT_QMAKE_EXECUTABLE` points to `C:/Qt/4.8.6/bin/qmake.exe`. Configure again.
  * Generate
  * Open with VS2015
  * Build

### Compile ParaUnity plugin
This is a plugin for ParaView that comes from [here](https://github.com/RCBiczok/ParaUnity). It is developed for Qt 4.8 and ParaView 5, and that is the main reason why we are using those older versions instead of Qt5 and ParaView 5.4.
*Note:* I tried to make it work with ParaView 5.4, the plugin builds and loads, but no data is passed from ParaView to Unity, and debugging is probably not worth it.

In order to build ParaUnity do the following:

  * Open a terminal in `\ParaUnity\Unity3DPlugin`
  * `mkdir build`
  * `cd build`
  * `cmake -G "Visual Studio 14 2015 Win64" -DParaView_DIR="<PARAVIEW_DIR>\build" ..` (*`<PARAVIEW_DIR>` is most likely `../../../../ParaView-5.2.0`*)
  * Open `\ParaUnity\ParaView\Unity3DPlugin\build\Project.sln` in Visual Studio
  * Right click on the project `Unity3D`, go to `C/C++ > Additional Include Directories` and add `C:\Qt\4.8.6\include\QtNetwork`
  * Build
  * You now have some files (most importantly a `Unity3D.dll` file) in `/build/Debug`. Remember their location.

## Running with a scene

### Prerequisites
  * A prepared Unity scene with the appropriate GameObjects. Mine is found [here](https://github.com/vrcranfield/unity-menu).
  * Unity 5.6.1f1

### Exporting the Unity Scene
This is necessary, and has to be repeated every time there's a change in the Unity scene, as the plugin right now is only able to load a specific `exe` in the correct folder.

Once the scene is set up, from the Unity IDE do the following:
  * `File > Build Settings`
  * Make sure only the scene you are interested in is checked
  * Set `Target Platform` as `Windows` and `Architecture` as `x86_64`.
  * Hit build
  * Choose the same location as the `Unity3D.dll` (something like `\ParaUnity\Unity3DPlugin\build\Debug`)
  * Save the file as `unity_player.exe`. **The filename is important as right now it's hardcoded in the plugin!**

### Loading the plugin into ParaView
This needs to be repeated every time paraview is rebuilt. Otherwise it should be persistent.

  * Open ParaView 5.2.0 (from `paraview.exe` in the `\build\bin\Debug` folder, or from Visual Studio)
  * Go to `Tools > Manage Plugins`, click `Load New` and locate `Unity3D.dll`.
  * Open the Dropdown from Unity3D and select `Auto Load`.

### Running the plugin
If everything is set up correctly, the following should give results:

  * Load any file in ParaView (e.g. a simple sphere)
  * Click the unity button with the `P`
  * You should see your unity scene with the ParaView object in the middle.
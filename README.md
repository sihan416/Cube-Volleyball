NOTE: please fill in the first section with information about your game.

# Robotics

Cave Miner is Sihan Wang's implementation of [Robot Fun Police](http://graphics.cs.cmu.edu/courses/15-466-f17/game2-designs/jmccann/) for game2 in 15-466-f17.

![alt text](screenshots/Screenshot.png)

## Asset Pipeline

The main assets of this game come from the provided blender file. They include a variety of meshes used to implement the game. A python script was used to load the vertex location and vertex color of the meshes into a meshes data structure in OpenGL. For simplicity reasons, instead of avoiding duplicate meshses, the script I used simply copied every single mesh present in the blender file which means crates were duplicated to be each of their own mesh.

## Architecture

Most of the implementation was fairly straight forward. The scene was an unordered map of objects to be rendered. I used very simple lighting for the fragment shader and no material. I did not actually end up using hierarchy because I found lambda functions could not be recursive so it was simply easier to hard code. Rotations were performed using the rotate functiosn for quarternions and vectors. I found a very conveinet reflect point over vector function in the experimental glm library. Each object has its own position and rotations which allowed for independent manipulation of the objects in the world. The local to world and other conversions were used to determine the axis of rotation.

## Reflection

In practicality it was not too difficult, however, I ended up on a 24 hour (I did pass out for a couple hours in the middle) straight hack session. (I know I said I would get it in by 8 in the morning but I kind of fell behind and then passed out around 8:30AM). The main problem was I completely misunderstood the matrixes used for conversions so I could not figure out why my object was not rotating properly for 8+ hours. Clearly this would not be an issue again in the future. The math performed is also poorly written as a result of this. I am submitting this as is due to the fact I am really out of time. If I had more time I would have rewritten the rotations to use hierarchy and cleaned up the math. In addition, I did not acutally bound any of the robotic arm rotations so there is a lot of potential clipping. That is something else I would like to address if I had more time.

The design document was clear in most aspects, the only ambiguity was how collision was expected to work. I asksed about this in office hours, but in the end decided to simply ignore it due to time constraints and because it does not really detract from the mechanics of the game.


# About Base2

This game is based on Base2, starter code for game2 in the 15-466-f17 course. It was developed by Jim McCann, and is released into the public domain.

## Requirements

 - modern C++ compiler
 - glm
 - libSDL2
 - libpng
 - blender (for mesh export script)

On Linux or OSX these requirements should be available from your package manager without too much hassle.

## Building

This code has been set up to be built with [FT jam](https://www.freetype.org/jam/).

### Getting Jam

For more information on Jam, see the [Jam Documentation](https://www.perforce.com/documentation/jam-documentation) page at Perforce, which includes both reference documentation and a getting started guide.

On unixish OSs, Jam is available from your package manager:
```
	brew install ftjam #on OSX
	apt get ftjam #on Debian-ish Linux
```

On Windows, you can get a binary [from sourceforge](https://sourceforge.net/projects/freetype/files/ftjam/2.5.2/ftjam-2.5.2-win32.zip/download),
and put it somewhere in your `%PATH%`.
(Possibly: also set the `JAM_TOOLSET` variable to `VISUALC`.)

### Bulding
Open a terminal (on windows, a Visual Studio Command Prompt), change to this directory, and type:
```
	jam
```

### Building (local libs)

Depending on your OSX, clone 
[kit-libs-linux](https://github.com/ixchow/kit-libs-linux),
[kit-libs-osx](https://github.com/ixchow/kit-libs-osx),
or [kit-libs-win](https://github.com/ixchow/kit-libs-win)
as a subdirectory of the current directory.

The Jamfile sets up library and header search paths such that local libraries will be preferred over system libraries.

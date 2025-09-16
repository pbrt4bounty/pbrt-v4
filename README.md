### Why this fork..??

Some personal reasons to created this fork..(wip)

I"ve always wanted to learn the techniques of rendering, since the first time I saw a Pov-Ray 2.0 render.
That scene that was slowly drawn, line by line, on the screen of an old 386 sx,
until it completed an image of a checkered plane, on which rested a red, bright and perfect sphere.
Thus, this project is just an excuse to continue learning in the best possible way: by practicing.

### Changes on this fork/branch

This branch include some experimental additions.
From other forks:
  - Intel OpenPGL implementation from https://github.com/OpenPathGuidingLibrary/pbrt-v4
  - VSPG implementation, based on OpenPGL from: https://github.com/kehanxuuu/vspg-pbrt-v4
  - a few experimental implementations from: https://github.com/rOnInRaJ-dev/pbrt-v4.git
    - Procedural Mesh
    - God rays
    - Bilateral filter(for SPPM..?)
  - Camera shift and tilt(searching the fork link..)

My small contributions..
  - Added Progresbar for interactive mode using ImGui
  - Added the ability to set the different EXR channels defined in the GBuffer as user defined AOVS.
  - Added support to use the binary hair definition files from Cem Yuksel format( only for CPU..) https://github.com/cemyuksel/cyCodeBase
  - (wip..)

### Why an integration in Blender..??

Because Blender is a great open-source project, that allows people to learn about developing complex software applications.
The purpose of this integration is to make it easier to create scenes, define materials, and render them easily with pbrt.
Although there is no defined roadmap, this is a list of the things I would like to implement and their current status.


#### Geometry:

The geometry is exported in the compact and efficient binary PLY format.

  - [x] Meshes.
  - [x] Mesh Lights
  - [x] Extruded Curves ( as polygon mesh..)
  - [x] MetaBalls ( as polygon mesh..)
  - [x] Instances
  - [x] Proxys ( as instances..)
  - [x] Hair ( as instantiated object)
  - [x] Binary hair files directly to core(Cem Yuksel .hair format)
  - [x] Native Blender hair to .pbrt format to use in GPU mode or also to binary .hair to use with CPU mode.
  - [ ] Point Clouds (as instantiated object. wip)

#### Materials:

All PBRT materials are currently supported and some other from other fork's.

Translate and implementing the pbrt materials using nodes is not a easy task. But anyway is work in progress..

  - [x] Conductor
  - [x] Coatted Conductor
  - [x] Diffuse
  - [x] Coated Diffuse
  - [x] Subsurface
  - [x] Dielectric
  - [x] ThinDielectric
  - [x] Hair
  - [x] Measured
  - [x] Mix
  - [x] Diffuse Transmission
  - [x] Cook Torrance(Added by Intel OpenPGL implementation..)

#### Textures:

  - [x] Image
  - [x] Marble
  - [x] Dots(color & float)
  - [x] Wrinkled
  - [x] Windy
  - [x] Fbm
  - [x] Mix(color & float)
  - [x] Checkerboard(color & float)
  - [x] Bilerp(color & float)
  
#### Cameras

  - [x] Perspective
  - [x] Orthographic
  - [x] Realistic
  - [x] Spherical
  
#### Volumes

  - [x] Cloud Grid
  - [x] NanoVDB
  - [x] Uniform Grid
  - [ ] Blender native Smoke simulations( I need manage .vdb Blender cache files to .nvdb)

#### General

  - [x] Multi-material per object(spliting geometry per-material index)
  - [x] Texture 'alpha' ( like as 'sprite' concept.)
  - [x] MotionBlur
  - [x] Animation
  - [x] Material Preview( even Hair shape type)
  - [x] PlyMesh Displacement(with texture)

#### Others

  - [x] Manage to compile using LLVM & Clang 14 under Windows 10 (not GPU..)
  - [x] Build as .dll to embed into other applications.
  - [ ] Create a c++ API(low priority..)
    

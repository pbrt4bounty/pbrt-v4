### Why this fork..??

Some personal reasons to created this fork..(wip)

I"ve always wanted to learn the techniques of rendering, since the first time I saw a Pov-Ray 2.0 render.
That scene that was slowly drawn, line by line, on the screen of an old 386 sx,
until it completed an image of a checkered plane, on which rested a red, bright and perfect sphere.
Thus, this project is just an excuse to continue learning in the best possible way: by practicing.

### Why an integration in Blender..??

Because Blender is a great open-source project, that allows people to learn about developing complex software applications.
The purpose of this integration is to make it easier to create scenes, define materials, and render them easily with pbrt.
Although there is no defined roadmap, this is a list of the things I would like to implement and their current status.


#### Geometry:

The geometry is exported in the compact and efficient binary PLY format.

  - [x] Meshes.
  - [x] Extruded Curves ( as polygon mesh..)
  - [x] MetaBalls ( as polygon mesh..)
  - [x] Instances
  - [x] Proxys ( as instances..)
  - [x] Hair ( as instantiated object)
  - [x] Binary hair files directly to core(Cem Yuksel .hair format)
  - [x] Native Blender hair to .pbrt format to use in GPU mode or also to binary .hair to use with CPU mode.
  - [ ] Point Clouds (as instantiated object. wip)

#### Materials:

Almost all PBRT materials are currently supported, although some use the default values.

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

#### Textures:

  - [x] Image
  - [x] Marble
  - [x] Dots
  - [x] Wrinkled
  
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
    

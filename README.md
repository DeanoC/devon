![](https://github.com/DeanoC/devon/Build/badge.svg)

# Devon -Texture viewer using al2o3 and the-forge #

Currently it can do ordinary formats via @nothings stb. EXR via @syoyo tiny exr. Basis u files via @richgel999 . tiny ktx and dds. Can decompress BC1-5 + 7 + ETC1 + ASTC LDR if no HW support. Use @TheForge_FX for rendering.

RGBA selector and signed viewing. View each array slices and mip map level.

TinyImageFormat does the pixel image format decoding if GPU doesn't support a particular format

To build with CMake just do your normal IDE or command line. It will take a while first time as it will download all teh dependencies and compile them. You will get an al2o3 folder one level up from where you clone this (this can be changed), the al2o3 holds all the git cloned dependecies folders. 

Use the al2o3 image libraries for the majority of the work.

ImageIO does the loading

Image handles format conversion 

ImageDecompres handle compressed format decompression.

Rendering is done using TheForge, currently Windows D3D12 is the tested platform. MacOs via metal is tempororily broken but is normally a working tests configuration. Has worked in the past with Windows Vulkan.
Linux Vulkan and Windows Dx11 will likely be supported in the future

TODO
----
Multiple files open

About with all license info of all the libraries

Use enki tasks for the decompression

Drag and Drop

BC6H software decoder

ECT2 software decoder

ATC? decoder

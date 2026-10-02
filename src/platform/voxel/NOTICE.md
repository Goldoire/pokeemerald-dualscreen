# Voxel contributions and reused work

The existing native world, classification, carving and shape work follows
the DramaticShapes algorithms credited in the corresponding source files.
That attribution remains in place.

The fixed northwest sun (`DX=0.85`, `DZ=0.55`), ambient floor (0.70), and
`VoxelLight_Face` calculation in `voxel_light.c` are adapted from
ZallaxDev/pokeemerald-3Ds-dualscreen at commit
`b27f5c64ee592ca22a3da093251305dcd6de7cd6`,
`3ds_port/src/voxel/voxel_lighting.c` and `.h`.
The donor's scoped MIT notice is preserved in `ZALLAX-LICENSE.txt`.

Our CPU triangle trees, renderer integration, shared hull-template casters
and receiving-plane sprite-shadow clipping are implemented for this port's
GL/GLES geometry and lifecycle. They do not use the donor's Citro3D resource
allocator, tree cards, generated terrain representations or 3DS runtime.

Generated Pokémon art, textures and models retain their original ownership;
the port license does not relicense game content.

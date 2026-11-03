# Models

Drop `.gltf`, `.glb`, `.obj` or `.fbx` files here (keep a model's textures, `.bin` and `.mtl` files next to it).

The build copies this folder to `<exe folder>/assets/Models`, and Hephaestian loads every model it finds there at startup,
lining them up in front of the camera.

- glTF / GLB: imported with [fastgltf](https://github.com/spnda/fastgltf)
- OBJ / FBX: imported with [ufbx](https://github.com/ufbx/ufbx)

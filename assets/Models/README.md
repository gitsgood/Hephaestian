# Models

Drop `.gltf`, `.glb`, `.obj` or `.fbx` files here (keep a model's textures, `.bin` and `.mtl` files next to it).

The build copies this folder to `<exe folder>/assets/Models`. Models aren't loaded on their own: a scene file
(`assets/Scenes/*.json`) names them in its `assets.models` table, e.g. `"Duck": "Models/Duck.glb"`, and places them
with its entities.

- glTF / GLB: imported with [fastgltf](https://github.com/spnda/fastgltf)
- OBJ / FBX: imported with [ufbx](https://github.com/ufbx/ufbx)

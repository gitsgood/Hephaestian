# Terrain

Raw terrain data (point clouds / scans). Files here are NOT auto-loaded as models: they are processed into
surfaces at startup (ground filter -> regular grid triangulation -> B-spline surface), see `src/Terrain`.

- `laz_mesh.obj`: airborne LiDAR scan of a quarry (~100 x 50 m, 1.06M points, y-up). Only its `v` lines are used.

# Sprites

Sprite sheets: an image plus a JSON file describing its frames and animation clips. Scene files list them in their
`assets.spriteSheets` table and draw them with `sprite` / `spriteAnimation` entity blocks (see `include/Hephaestian/ECS/SceneFile.h`).

Two ways to describe a sheet (full details in `include/Hephaestian/Assets/SpriteSheet.h`):

- **A grid sidecar**, `<name>.spritesheet.json`: the image, the grid of equal frames, pixels per unit, filtering and clips.
  `Slime.spritesheet.json` is one.
- **An Aseprite JSON export** (File > Export Sprite Sheet, JSON Data, Array or Hash): every frame keeps its own rectangle and
  duration, every tag becomes a clip. `Coin.json` is one (Hash layout). A sidecar can also point at one with `"aseprite"` to
  change its settings or add clips.

- **A folder of pages**: long animations spread over several images. The folder holds the images plus one
  `.spritesheet.json` with `"imageFolder": "."` (every image in it, ordered by the number ending each name) and the grid
  each page is cut with; frames are numbered across the pages. Scenes can name the folder itself as the sheet.
  Empty cells at the end of the last page are dropped, `"transparentColour"` keys out a background colour for images
  without transparency, and a clip with no `frames`/`range` plays everything. `LaFemme/` and `Ricardo/` are two.

Any sidecar can set `"blending": "additive"`: the art is added onto what's behind it, so black vanishes and bright parts
glow (light, fire, sparks painted on black). `LaFemme/` uses it. The default, `"cutout"`, shows each pixel fully or not at all.

`Slime.png` and `Coin.png` are placeholder art, drawn by a script.

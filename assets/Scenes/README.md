# Scenes

Every `.json` in here is a scene, and gets its own load button in the "Hephaestian Settings" window.
`startupScene` in `HephaestianSettings.json` (or `--scene <path>` on the command line) picks the one opened at startup.

The layout is documented at the top of `include/Hephaestian/ECS/SceneFile.h`:
- `assets`: names for model and texture files (paths relative to `assets/`)
- `entities`: `id`, optional `name` and `parent` (another entity's id), `position`, `rotationInDegrees`, `scale`,
  `model` *or* `primitive` (`quad`, `cube`, `sphere`), optional `texture` override, and optional `camera`,
  `freeFlyController` and `constantRotation` blocks
- `activeCamera`: the id of the camera to look through
- `clearColour`, `lightPosition`, `lightColour`: optional, they override the engine settings while the scene is loaded

Nothing writes to these files on its own: "Save scene" in the settings window does, on purpose.

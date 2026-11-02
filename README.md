# MacroBot

Records your inputs frame by frame and replays them. Works with practice-mode checkpoints.

## Usage
1. Pause the level, press **Record**, then **restart** the level (so recording starts at frame 0).
2. Play. Dying in practice mode rewinds the macro to the checkpoint.
3. Pause → **Record** again to stop, **Save** to write it to disk.
4. To replay: pause → **Play**, then restart the level. **Load** restores the saved macro for the level.

## Tips
- Turn off Click Between Steps in GD settings.
- Local levels all share ID 0, so their macros collide.

## Build
Install the Geode SDK v6.0.0-alpha.1 (`geode sdk install` / `geode sdk update`), then `geode build`.
Or push to GitHub and run the included workflow.

Before publishing, change `id` and `developer` in `mod.json`.

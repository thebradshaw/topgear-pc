## What's new in 1.2

This build brings in a lot of player feedback, with more on the way. Here is everything that has changed since the first release (0.1.0).

### High frame rate and smooth motion now keep the original game's pace
With smooth motion or high frame rate on, the game updates the race three times as often as the original. Some things were still applied once per update, so they happened about three times too fast. They now happen at the same pace as the original game:
- **Other cars weaving side to side.** Before, the computer cars swerved across the road far too quickly.
- **Pit stop refuelling.** Before, the tank filled about three times too fast.
- **Speeding up and slowing down.** Acceleration, braking, drag and the pull of hills now match the original, for you and for the rival car. On a test race, the rival's speed now climbs at the same rate as with smooth motion off, within about 1%. Before, it reached top speed in about a third of the time.
- **Skids and running off the road.** Recovering from a skid and slowing down on the grass now take as long as in the original.
- **Bumps.** Cars you bump into ease back to their speed over the same time as in the original.
- **Start and finish.** The engine revs settle on the start line at the original rate, and the pause after you cross the finish line is the original length.

Fuel use was already correct and hasn't changed. With smooth motion off, the game behaves exactly as before.

### VSync fix
Before, VSync was only on with high frame rate, so normal play could tear. VSync is now its own option, on by default. You can change it in the launcher under Display, in the in-game Tab menu, or turn it off with `--no-vsync`. The game runs at the same speed either way. With high frame rate on, VSync shows one smooth in-between picture per display refresh; with it off, the game draws as many as the PC can manage.

### Launcher
- The launcher's icon and Top Gear logo are now built into every download, so they always show, even if the art files aren't next to the launcher.

### Downloads
- The download buttons at the top of the GitHub page always point to the newest Windows and Linux builds.

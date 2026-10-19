# Input Buffer

Click a little too early and it still counts. If you click while the click
can't do anything yet (in the air as cube, robot, ball or spider, and not
touching an orb), the mod remembers it for 2 to 8 frames. The moment you
land or reach an orb inside that window, the click is used once. It never
turns into an extra jump, and if the window runs out the click is dropped.

- **Buffer Window**: 2-8 frames at 60 FPS (about 33-133 ms).
- Ship, wave, UFO and swing aren't buffered, since a click already does
  something there.
- **Safe Mode** (on by default): plays levels in test mode so runs with the
  buffer aren't saved as real completions.

# Darts Atlas for OBS

Version 0.1.0 source for an OBS dock targeting OBS Studio 32.0.4. The first test package is for macOS. The same C++/Qt plugin and themes are intended for Windows in a later build.

## Build the Mac test package

A developer can place these files at the root of a GitHub repository and run **Actions → Build macOS test plugin → Run workflow**. Once the run succeeds, download the artifact and extract the inner `darts-atlas-obs-macos.zip`. The workflow targets both Apple Silicon and Intel and uses the OBS plugin template's ad-hoc signing defaults for testing.

For a local build, use macOS with full Xcode selected and CMake 3.28 or later. From the project folder, run:

```sh
cmake --preset macos
cmake --build --preset macos
```

The bundle is in `build_macos/rundir/RelWithDebInfo/darts-atlas-obs.plugin`.

## Install and test

1. Quit OBS. In Finder, choose **Go → Go to Folder** and enter `~/Library/Application Support/obs-studio/`. Create a `plugins` folder if needed and place the built `darts-atlas-obs.plugin` bundle inside it.
2. Start OBS 32.0.4. In a test scene collection, open **Docks → Darts Atlas**.
3. Paste `https://www.dartsatlas.com/tournaments/7KRDAKwJXYaO`, choose **DA Default** and click **Load**. Check that **Darts Atlas - Group Stage** shows `/live_groups` and **Darts Atlas - Knockout** uses `/bracket`. The latter needs a tournament with a generated knockout bracket.
4. Change to **BHDP Teal** and click **Load**. Check the existing two scenes for the theme and confirm that there are no duplicates. Repeat after restarting OBS.
5. In Studio Mode, click **Load** and confirm that Group Stage opens in Preview. Use OBS's **Fullscreen Projector (Preview)** to select your second display. Select Knockout in Preview after Darts Atlas has generated the bracket.
6. To test a new event, paste another tournament URL and check that both sources point to it. To test error handling, enter an invalid URL and check that the previous scenes and sources remain in place.

The plugin uses OBS's base canvas dimensions for its Browser Sources. Start testing at 1920 × 1080 and check the layout against the venue's display size. The status message confirms local scene setup. Check Preview to confirm that the live pages render.

## Add another theme

Hover over the **Theme** dropdown to see the local theme folder path. Put a subfolder there named after the theme, containing `live_groups.css` and `bracket.css`. Close and reopen the dock to refresh the list, select the new theme and click **Load**. The built-in BHDP Teal theme uses the supplied CSS for each page.

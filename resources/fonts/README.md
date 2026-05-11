# Fonts

The plugin's UI uses two open-source typefaces:

- **Inter** — body / heading / display text
  https://rsms.me/inter/ — SIL OFL 1.1
  Drop into this folder:
    - `Inter-Regular.ttf`
    - `Inter-Medium.ttf`
    - `Inter-SemiBold.ttf`
    - `Inter-OFL.txt` (the license text from the upstream zip)

- **JetBrains Mono** — monospace text (file paths, timings)
  https://www.jetbrains.com/lp/mono/ — SIL OFL 1.1
  Drop into this folder:
    - `JetBrainsMono-Regular.ttf`
    - `JetBrainsMono-OFL.txt` (the license text from the upstream zip)

These files are NOT committed to keep the repo lightweight. Until you drop
them in, the build will fail at the `juce_add_binary_data(StemmerizerData)`
step.

## How CMake currently handles them

`CMakeLists.txt` lists the four `.ttf` filenames as required `SOURCES` for
`juce_add_binary_data`. If a file is missing, CMake errors out at configure
time with a clear "file not found" message — there is no silent fallback,
because shipping with system fonts produces inconsistent rendering across
buyers' machines and we don't want that surprise after launch.

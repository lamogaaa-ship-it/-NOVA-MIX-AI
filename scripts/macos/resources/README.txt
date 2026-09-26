NOVA MIX AI - macOS installation
================================

1. Double-click "Install NOVA MIX AI.pkg".
   Developer builds are not notarized by Apple yet. If macOS says the installer
   "cannot be opened because it is from an unidentified developer":
     - right-click (Control-click) the .pkg and choose "Open", then "Open" again, or
     - open System Settings > Privacy & Security and click "Open Anyway".
2. Keep all three components selected (VST3, Audio Unit, Standalone) or use Customize.
3. Installed locations:
     VST3        /Library/Audio/Plug-Ins/VST3/NOVA MIX AI.vst3
     Audio Unit  /Library/Audio/Plug-Ins/Components/NOVA MIX AI.component
     App         /Applications/NOVA MIX AI.app
4. FL Studio: Options > Manage plugins > "Find more plugins" (or "Find installed plugins"),
   then add NOVA MIX AI to a mixer insert on your vocal or master track.
   Logic Pro: the Audio Unit appears under Audio Units > NOVA Audio.

AI engineer
- NOVA's offline engineer works without any account or internet connection.
- The cloud engineer (Claude) is optional. Configure it in NOVA's settings (gear icon):
  a NOVA Cloud URL/token, or a Claude API key for developer builds. Audio never leaves
  your computer; only analysis numbers and your text are sent when the cloud engineer is on.

Voice (optional)
- Install "NOVA Companion" (second installer in the complete disk image) for push-to-talk in
  English and Arabic and spoken replies. It runs in the background on this Mac only
  (127.0.0.1) and starts at login. The first time you hold the talk button macOS asks for
  microphone access, and the speech model (about 460 MB) downloads once.
- Without it everything else works; the talk button shows "Voice off".

Uninstall
- Delete the three items listed above, and optionally ~/Library/Application Support/NOVA MIX AI.

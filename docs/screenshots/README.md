# Screenshots

The images linked from the main README were captured from the current `PSP5_TEST` build in PPSSPP 1.20.4 with the D3D11 renderer. Test content (games, apps, music, pictures, and saved data) is generated sample data. Console details are simulated, and the MAC address in that screenshot is masked.

The remaining screenshots in this directory predate this capture pass and have not all been checked against the current build. Do not treat them as current UI references until reviewed.

To refresh the gallery, build the test version with `tools/build.sh test`, then run `tools/ppsspp-test.ps1` with `tools/tests/readme_gallery.txt`, `tools/tests/standby.txt`, `tools/tests/readme_file_manager.txt`, `tools/tests/readme_categories.txt`, `tools/tests/savedata.txt`, and `tools/tests/readme_console_info.txt`. The capture script checks that PPSSPP is the foreground window before taking each screenshot.

# TracyX third-party notices

TracyX is a fork of Tracy v0.15.0. Tracy's BSD 3-Clause terms are in `LICENSE`.
Binary packages include the corresponding license texts under `licenses/`.
The notices and license texts must accompany redistributed binaries.

| Component | Pinned source or version | Role | Package license location |
| --- | --- | --- | --- |
| [DxTimingCaptureLibrary](https://github.com/microsoft/DxTimingCaptureLibrary) | `72bd2b18553493aa9850e434caf71bfa18952aac` | DX ETW decoding, statically linked into Agent | `licenses/DxTimingCaptureLibrary/` |
| [WinPixEventRuntime](https://www.nuget.org/packages/WinPixEventRuntime/1.0.240308001) | `1.0.240308001` | Optional Probe PIX runtime | `licenses/WinPixEventRuntime/` when packaged |
| [PresentMon](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0) | `2.6.0` | Optional SDK loader DLL and external service | `licenses/PresentMon-LICENSE.txt` when packaged |
| Tracy Viewer CMake vendors | Versions fixed in the source tree's `cmake/vendor.cmake` | Viewer static dependencies | `licenses/{component}/` |
| libbacktrace | Tracy source tree | Tracy Client stack traces | `licenses/libbacktrace-LICENSE` |

The Viewer vendor license folders cover base64, Capstone, FreeType, GLFW, ImGui,
nlohmann/json, libcurl, md4c, nativefiledialog-extended, PPQSort, pugixml,
tidy-html5, usearch and zstd. The packaging script copies license and notice
files from the exact sources used by the Viewer build. Some vendors have
additional licenses for nested code; those files are included in the same
component folder. See the copied files for the complete terms.

Microsoft PIX, the PresentMon service, and the D3D12 Agility SDK are installed
separately. They are not included in the TracyX binary archive. The package
script refuses to include an optional DLL when its corresponding license text
has not been supplied.

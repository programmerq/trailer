# TODO: Windows application icon

Create `trailer.ico` with the following sizes (all in a single multi-resolution .ico file):

- 256x256 (used by Explorer for large icon view)
- 128x128
- 48x48  (used by Explorer for medium icon view)
- 32x32  (used by taskbar)
- 16x16  (used by title bar and small icon view)

## How to reference it in WiX

Add an `<Icon>` element to `trailer.wxs` inside the `<Product>` element:

```xml
<Icon Id="TrailerIcon" SourceFile="platform/windows/trailer.ico" />
<Property Id="ARPPRODUCTICON" Value="TrailerIcon" />
```

Then update the `DefaultIcon` registry values in `CapabilitiesComponent` to point to the real
icon instead of `trailer.exe,0` (or keep the exe reference if the .ico is embedded as a resource).

## How to embed the icon in the .exe

In `CMakeLists.txt`, add a Windows resource file:

```cmake
if(WIN32)
    target_sources(trailer PRIVATE platform/windows/trailer.rc)
endif()
```

Where `trailer.rc` contains:

```rc
IDI_ICON1 ICON "platform/windows/trailer.ico"
```

## Recommended tools

- [ImageMagick](https://imagemagick.org): `magick convert input.png -define icon:auto-resize=256,128,48,32,16 trailer.ico`
- [icotool](https://www.nongnu.org/icoutils/): part of icoutils, Linux-native

## Related TODOs

- [ ] Add Authenticode code signing (`signtool.exe /f cert.pfx /p password /t http://timestamp.digicert.com`)
      to `scripts/build-windows-msi-inner.sh` once a certificate is available
- [ ] Add Windows MSI build + signing to CI once certificate is provisioned
- [ ] Evaluate WiX 4 (.NET-based, cross-platform wix.exe) for future once msitools/wixl
      support matures or the project moves to a .NET-capable CI environment

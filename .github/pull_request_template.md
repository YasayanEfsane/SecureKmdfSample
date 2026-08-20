## Summary

Describe the focused change and why it is needed.

## Security impact

- [ ] `METHOD_BUFFERED`, exact size checks, and the pointer-free ABI are preserved.
- [ ] Access control and PASSIVE_LEVEL execution requirements are preserved.
- [ ] Any protocol change is versioned and documented.
- [ ] No secrets, private dumps, or undisclosed vulnerability details are included.

## Validation

- [ ] `Debug | x64` builds with Visual Studio and the WDK.
- [ ] `Release | x64` builds with Visual Studio and the WDK.
- [ ] Offline protocol tests pass.
- [ ] Driver integration tests pass in an isolated elevated test VM, or the reason they were not run is documented below.
- [ ] Documentation and changelog are updated where applicable.

## Test environment and evidence

List the Windows build, Visual Studio/WDK versions, commands, and relevant results.

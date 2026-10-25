# CI scope

- Use pinned upstream QEMU, official configure/Make/Meson/Ninja and C/libqtest; do not add QMP clients or emulator-control frameworks.
- CI generates/runs small synthetic ARM fixtures only. Do not download factory firmware, invoke AI or add custom digest calculations/gates.
- Check machine registration, initialization, reset and actual continue/stop. Synthetic success does not establish original MAS or complete-camera boot.
- Keep build/test output ignored and use short temporary socket paths. Preserve failures without overwriting original execution records.
- Use short single-word workflow filenames; the current entry is workflows/smoke.yml. Keep path changes synchronized with Make/native qtest registration and preserve historical run identities.

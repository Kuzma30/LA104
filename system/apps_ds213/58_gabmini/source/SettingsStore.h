#pragma once

// Settings kept between runs in MINIOSC.CFG on the disk of the device.
//
// Only values are stored (no pointers or vtables of the settings classes),
// field by field, with a version and a checksum. A damaged or foreign file
// keeps the defaults, a single value out of range keeps its default.
namespace SettingsStore
{
    // before the windows are created
    void Load();
    // when the application is closed
    void Save();
    // from the main loop: saves a few seconds after the last change, so the
    // settings survive switching the device off without leaving the app
    void Tick();
}

// .syx file storage and a lightweight patch-library model.
//
// Patches are stored as standard MIDI .syx files (raw SysEx bytes), so they
// interoperate with other SysEx tools/librarians. A file may hold a single
// patch dump or a whole "all patches" bulk stream (many dumps + skips).
#pragma once

#include <optional>
#include <string>

#include "Patch.h"
#include "SysEx.h"

namespace sidstation {

// ---- Raw .syx file I/O ----------------------------------------------------

// Writes `data` verbatim to `path`. Returns false on any I/O error.
bool writeSyxFile(const std::string &path, const Bytes &data);

// Reads a .syx file's raw bytes. Returns nullopt if it can't be read.
std::optional<Bytes> readSyxFile(const std::string &path);

// ---- SysEx / patch extraction --------------------------------------------

// Splits a .syx byte stream into its complete SysEx messages.
std::vector<Bytes> splitSysExMessages(const Bytes &data);

// Decodes every SidStation patch dump found in a byte stream (ignores skips and
// any non-patch messages). Useful for importing a bulk "all patches" dump.
std::vector<Patch> extractPatches(const Bytes &data);

// A single patch and the exact SysEx bytes that carry it - so one patch can be
// sent to the unit on its own (non-destructive), split out from a bank file.
struct PatchItem {
  std::string name; // decoded patch name
  Bytes message;    // the single patch-dump SysEx (F0..F7)
};

// Splits a byte stream into individual patch items (one per patch dump. Skips
// and all-clear messages are ignored).
std::vector<PatchItem> extractPatchItems(const Bytes &data);

// Convenience: save one patch to a .syx file / load one patch from a .syx file
// (the first patch dump found).
bool savePatchToFile(const std::string &path, const Patch &patch);
std::optional<Patch> loadPatchFromFile(const std::string &path);

// ---- Folder library -------------------------------------------------------

struct PatchEntry {
  std::string path; // absolute/relative path to the .syx file
  std::string name; // decoded patch name, or the filename if not decodable
  bool valid;       // true if the file decoded as a SidStation patch dump
};

// Scans `dir` (non-recursive) for *.syx files and returns an entry per file,
// sorted by filename. Non-.syx files are ignored.
std::vector<PatchEntry> scanPatchFolder(const std::string &dir);

} // namespace sidstation

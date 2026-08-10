#include "sidstation/SyxFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "sidstation/SysExStream.h"

namespace sidstation {
namespace fs = std::filesystem;

bool writeSyxFile(const std::string &path, const Bytes &data) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  if (!os)
    return false;
  os.write(reinterpret_cast<const char *>(data.data()),
           static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(os);
}

std::optional<Bytes> readSyxFile(const std::string &path) {
  std::ifstream is(path, std::ios::binary | std::ios::ate);
  if (!is)
    return std::nullopt;
  const auto size = is.tellg();
  if (size < 0)
    return std::nullopt;
  is.seekg(0);
  Bytes data(static_cast<std::size_t>(size));
  if (size > 0 && !is.read(reinterpret_cast<char *>(data.data()),
                           static_cast<std::streamsize>(size)))
    return std::nullopt;
  return data;
}

std::vector<Bytes> splitSysExMessages(const Bytes &data) {
  SysExAssembler asm_;
  return asm_.feed(data);
}

std::vector<Patch> extractPatches(const Bytes &data) {
  std::vector<Patch> patches;
  for (const auto &msg : splitSysExMessages(data)) {
    if (auto p = decodePatchDump(msg))
      patches.push_back(std::move(*p));
  }
  return patches;
}

std::vector<PatchItem> extractPatchItems(const Bytes &data) {
  std::vector<PatchItem> items;
  for (auto &msg : splitSysExMessages(data)) {
    if (auto p = decodePatchDump(msg))
      items.push_back({p->name(), msg});
  }
  return items;
}

bool savePatchToFile(const std::string &path, const Patch &patch) {
  return writeSyxFile(path, encodePatchDump(patch));
}

std::optional<Patch> loadPatchFromFile(const std::string &path) {
  auto data = readSyxFile(path);
  if (!data)
    return std::nullopt;
  auto patches = extractPatches(*data);
  if (patches.empty())
    return std::nullopt;
  return patches.front();
}

std::vector<PatchEntry> scanPatchFolder(const std::string &dir) {
  std::vector<PatchEntry> entries;
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return entries;

  for (const auto &item : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!item.is_regular_file())
      continue;
    auto ext = item.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (ext != ".syx")
      continue;

    PatchEntry entry;
    entry.path = item.path().string();
    entry.valid = false;
    entry.name = item.path().stem().string(); // fallback: filename

    if (auto patch = loadPatchFromFile(entry.path)) {
      entry.valid = true;
      if (!patch->name().empty())
        entry.name = patch->name();
    }
    entries.push_back(std::move(entry));
  }

  std::sort(
      entries.begin(), entries.end(),
      [](const PatchEntry &a, const PatchEntry &b) { return a.path < b.path; });
  return entries;
}

} // namespace sidstation

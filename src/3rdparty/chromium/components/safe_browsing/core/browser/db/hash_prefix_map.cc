// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/core/browser/db/hash_prefix_map.h"

#include "base/debug/crash_logging.h"
#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ref.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/threading/scoped_blocking_call.h"
#include "components/safe_browsing/core/browser/db/prefix_iterator.h"
#include "components/safe_browsing/core/common/features.h"

namespace safe_browsing {
namespace {

constexpr uint32_t kInvalidOffset = std::numeric_limits<uint32_t>::max();

// This is the max size of the offset map since only the first two bytes of the
// hash are used to compute the index.
constexpr size_t kMaxOffsetMapSize = std::numeric_limits<uint16_t>::max();

std::string GenerateExtension(PrefixSize size) {
  return base::StrCat(
      {base::NumberToString(size), "_",
       base::NumberToString(
           base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds())});
}

// Returns true if |hash_prefix| with PrefixSize |size| exists in |prefixes|.
bool HashPrefixMatches(base::StringPiece prefix,
                       HashPrefixesView prefixes,
                       PrefixSize size,
                       size_t start,
                       size_t end) {
  return std::binary_search(PrefixIterator(prefixes, start, size),
                            PrefixIterator(prefixes, end, size), prefix);
}

// Gets the index |prefix| should map to in an offset map of size |size|.
// The index is calculated as follows:
//  - Take the first 16 bits of the prefix.
//  - Divide that number evenly into |size| buckets.
size_t GetOffsetIndex(HashPrefixesView prefix, size_t size) {
  CHECK_GT(prefix.size(), 1u);
  uint16_t high = static_cast<uint8_t>(prefix[0]);
  uint16_t low = static_cast<uint8_t>(prefix[1]);
  uint16_t n = (high << 8) | low;
  return (n * size) / (std::numeric_limits<uint16_t>::max() + 1);
}

// Gets the size of the offset map based on the experiment configuration.
size_t GetOffsetMapSize(size_t file_size) {
  static const base::FeatureParam<int> kBytesPerOffsetParam{
      &kMmapSafeBrowsingDatabase, "store-bytes-per-offset", 0};
  size_t bytes_per_offset = kBytesPerOffsetParam.Get();
  if (!bytes_per_offset)
    return 0;
  return std::min(kMaxOffsetMapSize, file_size / bytes_per_offset);
}

// Builds the offset map for a prefix DB file.
class OffsetMapBuilder {
 public:
  explicit OffsetMapBuilder(PrefixSize prefix_size)
      : prefix_size_(prefix_size) {}

  void Reserve(size_t size) {
    offsets_.Resize(GetOffsetMapSize(size), kInvalidOffset);
  }

  // Add() may be called in two situations:
  //  - During a full update, where it will be called with the full hash prefix
  //    list. In this case we will use the size of hash prefix list passed in to
  //    determine the offset map size.
  //  - During a partial update, where it will be called for each hash prefix
  //    individually. In this case, Reserve() must have been called first to
  //    reserve space in the offset map.
  void Add(HashPrefixesView data) {
    // If space in the offset map hasn't been reserved and more than one prefix
    // is being added, reserve space now.
    if (offsets_.empty() && data.size() > prefix_size_)
      Reserve(data.size());

    if (offsets_.empty()) {
      cur_offset_ += data.size() / prefix_size_;
      return;
    }

    for (size_t i = 0; i < data.size(); i += prefix_size_) {
      size_t index = GetOffsetIndex(data.substr(i), offsets_.size());
      if (offsets_[index] == kInvalidOffset)
        offsets_[index] = cur_offset_;
      cur_offset_++;
    }
  }

  google::protobuf::RepeatedField<uint32_t> TakeOffsets() {
    // Backfill any empty spots with the value right after it.
    uint32_t last = cur_offset_;
    for (int i = offsets_.size() - 1; i >= 0; i--) {
      if (offsets_[i] == kInvalidOffset) {
        offsets_[i] = last;
      } else {
        last = offsets_[i];
      }
    }
    return std::move(offsets_);
  }

  size_t GetFileSize() const { return cur_offset_ * prefix_size_; }

 private:
  const PrefixSize prefix_size_;
  google::protobuf::RepeatedField<uint32_t> offsets_;
  size_t cur_offset_ = 0;
};

}  // namespace

InMemoryHashPrefixMap::InMemoryHashPrefixMap() = default;
InMemoryHashPrefixMap::~InMemoryHashPrefixMap() = default;

void InMemoryHashPrefixMap::Clear() {
  map_.clear();
}

HashPrefixMapView InMemoryHashPrefixMap::view() const {
  HashPrefixMapView view;
  view.reserve(map_.size());
  for (const auto& kv : map_)
    view.emplace(kv.first, kv.second);
  return view;
}

HashPrefixesView InMemoryHashPrefixMap::at(PrefixSize size) const {
  return map_.at(size);
}

void InMemoryHashPrefixMap::Append(PrefixSize size, HashPrefixesView prefix) {
  map_[size].append(prefix);
}

void InMemoryHashPrefixMap::Reserve(PrefixSize size, size_t capacity) {
  map_[size].reserve(capacity);
}

ApplyUpdateResult InMemoryHashPrefixMap::ReadFromDisk(
    const V4StoreFileFormat& file_format) {
  // This is currently handled in V4Store::UpdateHashPrefixMapFromAdditions().
  // TODO(cduvall): Move that logic here?
  DCHECK(file_format.hash_files().empty());
  return APPLY_UPDATE_SUCCESS;
}

namespace {

class InMemoryHashPrefixMapWriteSession : public HashPrefixMap::WriteSession {
 public:
  InMemoryHashPrefixMapWriteSession(
      std::unordered_map<PrefixSize, HashPrefixes>& map,
      ListUpdateResponse* lur)
      : map_(map), lur_(*lur) {}
  InMemoryHashPrefixMapWriteSession(const InMemoryHashPrefixMapWriteSession&) =
      delete;
  InMemoryHashPrefixMapWriteSession& operator=(
      const InMemoryHashPrefixMapWriteSession&) = delete;
  ~InMemoryHashPrefixMapWriteSession() override {
    auto addition_scanner = lur_->mutable_additions()->begin();
    // Move each raw hash from the `ListUpdateResponse` back into the map.
    for (auto& entry : *map_) {
      auto raw_hashes = base::WrapUnique(
          addition_scanner->mutable_raw_hashes()->release_raw_hashes());
      entry.second = std::move(*raw_hashes);
      ++addition_scanner;
    }
  }

 private:
  const raw_ref<std::unordered_map<PrefixSize, HashPrefixes>> map_;
  const raw_ref<ListUpdateResponse> lur_;
};

}  // namespace

std::unique_ptr<HashPrefixMap::WriteSession> InMemoryHashPrefixMap::WriteToDisk(
    V4StoreFileFormat* file_format) {
  ListUpdateResponse* const lur = file_format->mutable_list_update_response();
  // `file_format` is expected to not contain any additions at this point. It
  // will during migration from an MmapHashPrefixMap, but the map itself is
  // empty in that case so there is no data to move into/out of the session.
  CHECK(lur->additions_size() == 0 || map_.empty());
  for (auto& entry : map_) {
    ThreatEntrySet* additions = lur->add_additions();
    // TODO(vakh): Write RICE encoded hash prefixes on disk. Not doing so
    // currently since it takes a long time to decode them on startup, which
    // blocks resource load. See: http://crbug.com/654819
    additions->set_compression_type(RAW);
    additions->mutable_raw_hashes()->set_prefix_size(entry.first);

    // Avoid copying the raw hashes by temporarily moving them into
    // `file_format`. They will be returned to the map when the caller destroys
    // the returned write session.
    auto raw_hashes = std::make_unique<std::string>(std::move(entry.second));
    additions->mutable_raw_hashes()->set_allocated_raw_hashes(
        raw_hashes.release());
  }
  return std::make_unique<InMemoryHashPrefixMapWriteSession>(map_, lur);
}

ApplyUpdateResult InMemoryHashPrefixMap::IsValid() const {
  return APPLY_UPDATE_SUCCESS;
}

HashPrefixStr InMemoryHashPrefixMap::GetMatchingHashPrefix(
    base::StringPiece full_hash) {
  for (const auto& [size, prefixes] : map_) {
    base::StringPiece hash_prefix = full_hash.substr(0, size);
    if (HashPrefixMatches(hash_prefix, prefixes, size, 0,
                          prefixes.size() / size)) {
      return std::string(hash_prefix);
    }
  }
  return HashPrefixStr();
}

HashPrefixMap::MigrateResult InMemoryHashPrefixMap::MigrateFileFormat(
    const base::FilePath& store_path,
    V4StoreFileFormat* file_format) {
  if (file_format->hash_files().empty())
    return MigrateResult::kNotNeeded;

  ListUpdateResponse* lur = file_format->mutable_list_update_response();
  for (const auto& hash_file : file_format->hash_files()) {
    std::string contents;
    base::FilePath hashes_path =
        MmapHashPrefixMap::GetPath(store_path, hash_file.extension());
    if (!base::ReadFileToStringWithMaxSize(hashes_path, &contents,
                                           kMaxStoreSizeBytes)) {
      return MigrateResult::kFailure;
    }
    auto* additions = lur->add_additions();
    additions->set_compression_type(RAW);
    additions->mutable_raw_hashes()->set_prefix_size(hash_file.prefix_size());
    additions->mutable_raw_hashes()->set_raw_hashes(std::move(contents));
  }
  file_format->clear_hash_files();
  return MigrateResult::kSuccess;
}

void InMemoryHashPrefixMap::GetPrefixInfo(
    google::protobuf::RepeatedPtrField<
        DatabaseManagerInfo::DatabaseInfo::StoreInfo::PrefixSet>* prefix_sets) {
  for (const auto& size_and_prefixes : map_) {
    const PrefixSize& size = size_and_prefixes.first;
    const HashPrefixes& hash_prefixes = size_and_prefixes.second;

    DatabaseManagerInfo::DatabaseInfo::StoreInfo::PrefixSet* prefix_set =
        prefix_sets->Add();
    prefix_set->set_size(size);
    prefix_set->set_count(hash_prefixes.size() / size);
  }
}

// Writes a hash prefix file, and buffers writes to avoid a write call for each
// hash prefix. The file will be deleted if Finish() is never called.
class MmapHashPrefixMap::BufferedFileWriter {
 public:
  BufferedFileWriter(const base::FilePath& store_path,
                     PrefixSize prefix_size,
                     size_t buffer_size)
      : extension_(GenerateExtension(prefix_size)),
        path_(GetPath(store_path, extension_)),
        buffer_size_(buffer_size),
        offset_builder_(prefix_size),
        file_(path_, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE),
        has_error_(!file_.IsValid()) {
    buffer_.reserve(buffer_size);
  }

  ~BufferedFileWriter() {
    // File was never finished, delete now.
    if (file_.IsValid() || has_error_) {
      file_.Close();
      base::DeleteFile(path_);
    }
  }

  void Write(HashPrefixesView data) {
    if (has_error_)
      return;

    offset_builder_.Add(data);

    if (buffer_.size() + data.size() >= buffer_size_)
      Flush();

    if (data.size() > buffer_size_)
      WriteToFile(data);
    else
      buffer_.append(data);
  }

  bool Finish() {
    Flush();
    file_.Close();
    return !has_error_;
  }

  void Reserve(size_t size) { offset_builder_.Reserve(size); }

  google::protobuf::RepeatedField<uint32_t> TakeOffsets() {
    return offset_builder_.TakeOffsets();
  }

  size_t GetFileSize() const { return offset_builder_.GetFileSize(); }

  const std::string& extension() const { return extension_; }

 private:
  void Flush() {
    WriteToFile(buffer_);
    buffer_.clear();
  }

  void WriteToFile(HashPrefixesView data) {
    if (has_error_ || data.empty())
      return;

    if (!file_.WriteAtCurrentPosAndCheck(base::as_bytes(base::make_span(data))))
      has_error_ = true;
  }

  const std::string extension_;
  const base::FilePath path_;
  const size_t buffer_size_;
  OffsetMapBuilder offset_builder_;
  base::File file_;
  std::string buffer_;
  bool has_error_;
};

MmapHashPrefixMap::MmapHashPrefixMap(
    const base::FilePath& store_path,
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    size_t buffer_size)
    : store_path_(store_path),
      task_runner_(task_runner
                       ? std::move(task_runner)
                       : base::SequencedTaskRunner::GetCurrentDefault()),
      buffer_size_(buffer_size) {}

MmapHashPrefixMap::~MmapHashPrefixMap() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
}

void MmapHashPrefixMap::Clear() {
  if (kMmapSafeBrowsingDatabaseAsync.Get() &&
      !task_runner_->RunsTasksInCurrentSequence()) {
    // Clear the map on the db task runner, since the memory mapped files should
    // be destroyed on the same thread they were created. base::Unretained is
    // safe since the map is destroyed on the db task runner.
    task_runner_->PostTask(FROM_HERE,
                           base::BindOnce(&MmapHashPrefixMap::ClearOnTaskRunner,
                                          base::Unretained(this)));
  } else {
    map_.clear();
  }
}

void MmapHashPrefixMap::ClearOnTaskRunner() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  map_.clear();
}

HashPrefixMapView MmapHashPrefixMap::view() const {
  HashPrefixMapView view;
  view.reserve(map_.size());
  for (const auto& kv : map_) {
    if (!kv.second.IsReadable())
      continue;

    view.emplace(kv.first, kv.second.GetView());
  }
  return view;
}

HashPrefixesView MmapHashPrefixMap::at(PrefixSize size) const {
  const FileInfo& info = map_.at(size);
  CHECK(info.IsReadable());
  return info.GetView();
}

void MmapHashPrefixMap::Append(PrefixSize size, HashPrefixesView prefix) {
  if (prefix.empty())
    return;

  GetFileInfo(size).GetOrCreateWriter(buffer_size_)->Write(prefix);
}

void MmapHashPrefixMap::Reserve(PrefixSize size, size_t capacity) {
  GetFileInfo(size).GetOrCreateWriter(buffer_size_)->Reserve(capacity);
}

ApplyUpdateResult MmapHashPrefixMap::ReadFromDisk(
    const V4StoreFileFormat& file_format) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  DCHECK(file_format.list_update_response().additions().empty());
  for (const auto& hash_file : file_format.hash_files()) {
    PrefixSize prefix_size = hash_file.prefix_size();
    auto& file_info = GetFileInfo(prefix_size);
    if (!file_info.Initialize(hash_file)) {
      return MMAP_FAILURE;
    }
    static const base::FeatureParam<bool> kCheckMapSorted{
        &kMmapSafeBrowsingDatabase, "check-sb-map-sorted", true};
    if (kCheckMapSorted.Get()) {
      HashPrefixesView prefixes = file_info.GetView();
      uint32_t end = prefixes.size() / prefix_size;
      if (!std::is_sorted(PrefixIterator(prefixes, 0, prefix_size),
                          PrefixIterator(prefixes, end, prefix_size))) {
        return MMAP_FAILURE;
      }
    }
  }
  return APPLY_UPDATE_SUCCESS;
}

namespace {

class MmapHashPrefixMapWriteSession : public HashPrefixMap::WriteSession {
 public:
  MmapHashPrefixMapWriteSession() = default;
  MmapHashPrefixMapWriteSession(const MmapHashPrefixMapWriteSession&) = delete;
  MmapHashPrefixMapWriteSession& operator=(
      const MmapHashPrefixMapWriteSession&) = delete;
  ~MmapHashPrefixMapWriteSession() override = default;
};

}  // namespace

std::unique_ptr<HashPrefixMap::WriteSession> MmapHashPrefixMap::WriteToDisk(
    V4StoreFileFormat* file_format) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  for (auto& [size, file_info] : map_) {
    auto* hash_file = file_format->add_hash_files();
    if (!file_info.Finalize(hash_file))
      return nullptr;

    if (!file_info.Initialize(*hash_file))
      return nullptr;
  }
  return std::make_unique<MmapHashPrefixMapWriteSession>();
}

ApplyUpdateResult MmapHashPrefixMap::IsValid() const {
  for (const auto& kv : map_) {
    if (!kv.second.IsReadable())
      return MMAP_FAILURE;
  }
  return APPLY_UPDATE_SUCCESS;
}

HashPrefixStr MmapHashPrefixMap::GetMatchingHashPrefix(
    base::StringPiece full_hash) {
  for (const auto& kv : map_) {
    HashPrefixStr matching_prefix = kv.second.Matches(full_hash);
    if (!matching_prefix.empty())
      return matching_prefix;
  }
  return HashPrefixStr();
}

HashPrefixMap::MigrateResult MmapHashPrefixMap::MigrateFileFormat(
    const base::FilePath& store_path,
    V4StoreFileFormat* file_format) {
  // Check if the offset map needs to be updated. This should only happen if a
  // user switches to an experiment group with a different offset map size
  // parameter.
  bool offsets_updated = false;
  for (auto& hash_file : *file_format->mutable_hash_files()) {
    if (GetOffsetMapSize(hash_file.file_size()) ==
        static_cast<size_t>(hash_file.offsets().size())) {
      continue;
    }

    OffsetMapBuilder builder(hash_file.prefix_size());
    FileInfo info(store_path, hash_file.prefix_size());
    if (!info.Initialize(hash_file))
      return MigrateResult::kFailure;

    builder.Add(info.GetView());
    *hash_file.mutable_offsets() = builder.TakeOffsets();
    offsets_updated = true;
  }

  if (offsets_updated)
    return MigrateResult::kSuccess;

  ListUpdateResponse* lur = file_format->mutable_list_update_response();
  if (lur->additions().empty())
    return MigrateResult::kNotNeeded;

  for (auto& addition : *lur->mutable_additions()) {
    Append(addition.raw_hashes().prefix_size(),
           addition.raw_hashes().raw_hashes());
  }
  lur->clear_additions();
  return MigrateResult::kSuccess;
}

void MmapHashPrefixMap::GetPrefixInfo(
    google::protobuf::RepeatedPtrField<
        DatabaseManagerInfo::DatabaseInfo::StoreInfo::PrefixSet>* prefix_sets) {
  for (const auto& size_and_info : map_) {
    const PrefixSize& size = size_and_info.first;
    const FileInfo& info = size_and_info.second;

    DatabaseManagerInfo::DatabaseInfo::StoreInfo::PrefixSet* prefix_set =
        prefix_sets->Add();
    prefix_set->set_size(size);
    prefix_set->set_count(info.GetView().size() / size);
  }
}

// static
base::FilePath MmapHashPrefixMap::GetPath(const base::FilePath& store_path,
                                          const std::string& extension) {
  return store_path.AddExtensionASCII(extension);
}

const std::string& MmapHashPrefixMap::GetExtensionForTesting(PrefixSize size) {
  return GetFileInfo(size).GetExtensionForTesting();  // IN-TEST
}

void MmapHashPrefixMap::ClearAndWaitForTesting() {
  Clear();
  base::RunLoop run_loop;
  task_runner_->PostTask(FROM_HERE, run_loop.QuitClosure());
  run_loop.Run();
}

MmapHashPrefixMap::FileInfo& MmapHashPrefixMap::GetFileInfo(PrefixSize size) {
  auto [it, inserted] = map_.try_emplace(size, store_path_, size);
  return it->second;
}

MmapHashPrefixMap::FileInfo::FileInfo(const base::FilePath& store_path,
                                      PrefixSize size)
    : store_path_(store_path), prefix_size_(size) {}

MmapHashPrefixMap::FileInfo::~FileInfo() = default;

HashPrefixesView MmapHashPrefixMap::FileInfo::GetView() const {
  DCHECK(IsReadable());
  return HashPrefixesView(reinterpret_cast<const char*>(file_.data()),
                          file_.length());
}

bool MmapHashPrefixMap::FileInfo::Initialize(const HashFile& hash_file) {
  // Make sure file size is correct before attempting to mmap.
  int64_t file_size;
  base::FilePath path = GetPath(store_path_, hash_file.extension());
  if (!GetFileSize(path, &file_size)) {
    return false;
  }
  if (static_cast<uint64_t>(file_size) != hash_file.file_size()) {
    return false;
  }

  if (IsReadable()) {
    DCHECK_EQ(offsets_.size(), static_cast<size_t>(hash_file.offsets().size()));
    DCHECK_EQ(file_.length(), hash_file.file_size());
    return true;
  }

  if (!file_.Initialize(path)) {
    return false;
  }

  if (file_.length() != static_cast<size_t>(file_size)) {
    return false;
  }

  offsets_.assign(hash_file.offsets().begin(), hash_file.offsets().end());
  return true;
}

bool MmapHashPrefixMap::FileInfo::Finalize(HashFile* hash_file) {
  if (!writer_->Finish())
    return false;

  hash_file->set_prefix_size(prefix_size_);

  *hash_file->mutable_offsets() = writer_->TakeOffsets();
  hash_file->set_file_size(writer_->GetFileSize());
  hash_file->set_extension(writer_->extension());
  writer_.reset();
  return true;
}

HashPrefixStr MmapHashPrefixMap::FileInfo::Matches(
    base::StringPiece full_hash) const {
  HashPrefixStr hash_prefix(full_hash.substr(0, prefix_size_));
  HashPrefixesView prefixes = GetView();

  uint32_t start = 0;
  uint32_t end = prefixes.size() / prefix_size_;

  // Check the offset map to see if we can optimize the search.
  if (!offsets_.empty()) {
    size_t index = GetOffsetIndex(hash_prefix, offsets_.size());
    start = offsets_[index];
    if (++index < offsets_.size())
      end = offsets_[index];

    // If the start is the same as end, the hash doesn't exist.
    if (start == end)
      return HashPrefixStr();
  }

  // TODO(crbug.com/1409674): Remove crash logging.
  base::StringPiece start_prefix = prefixes.substr(0, prefix_size_);
  base::StringPiece end_prefix =
      prefixes.substr(prefix_size_ * (end - 1), prefix_size_);
  SCOPED_CRASH_KEY_STRING64(
      "SafeBrowsing", "prefix_match",
      base::StrCat({base::NumberToString(start), ":", base::NumberToString(end),
                    ":", base::NumberToString(prefix_size_), ":",
                    base::NumberToString(prefixes.size()), ":",
                    base::NumberToString(start_prefix.compare(hash_prefix)),
                    ":",
                    base::NumberToString(end_prefix.compare(hash_prefix))}));

  if (HashPrefixMatches(hash_prefix, prefixes, prefix_size_, start, end))
    return hash_prefix;
  return HashPrefixStr();
}

MmapHashPrefixMap::BufferedFileWriter*
MmapHashPrefixMap::FileInfo::GetOrCreateWriter(size_t buffer_size) {
  DCHECK(!file_.IsValid());
  if (!writer_) {
    writer_ = std::make_unique<BufferedFileWriter>(store_path_, prefix_size_,
                                                   buffer_size);
  }
  return writer_.get();
}

const std::string& MmapHashPrefixMap::FileInfo::GetExtensionForTesting() const {
  return writer_->extension();
}

}  // namespace safe_browsing

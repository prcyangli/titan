#pragma once

#include <stdint.h>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

#include "blob_file_cache.h"
#include "blob_storage.h"
#include "db/log_reader.h"
#include "db/log_writer.h"
#include "port/port_posix.h"
#include "rocksdb/options.h"
#include "rocksdb/status.h"
#include "titan/options.h"
#include "util/mutexlock.h"
#include "version_edit.h"

namespace rocksdb {
namespace titandb {

class VersionSet {
 public:
  explicit VersionSet(const TitanDBOptions& options);

  // Sets up the storage specified in "options.dirname".
  // If the manifest doesn't exist, it will create one.
  // If the manifest exists, it will recover from the latest one.
  // It is a corruption if the persistent storage contains data
  // outside of the provided column families.
  Status Open(const std::map<uint32_t, TitanCFOptions>& column_families);

  // Applies *edit on the current version to form a new version that is
  // both saved to the manifest and installed as the new current version.
  // REQUIRES: mutex is held
  Status LogAndApply(VersionEdit* edit);

  // Adds some column families with the specified options.
  // REQUIRES: mutex is held
  void AddColumnFamilies(
      const std::map<uint32_t, TitanCFOptions>& column_families);

  // Drops some column families. The obsolete files will be deleted in
  // background when they will not be accessed anymore.
  // REQUIRES: mutex is held
  Status DropColumnFamilies(const std::vector<uint32_t>& handles,
                            SequenceNumber obsolete_sequence);

  // Destroy the column family. Only after this is called, the obsolete files
  // of the dropped column family can be physical deleted.
  // REQUIRES: mutex is held
  Status DestroyColumnFamily(uint32_t cf_id);

  // Allocates a new file number.
  uint64_t NewFileNumber() { return next_file_number_.fetch_add(1); }

  // REQUIRES: mutex is held
  std::weak_ptr<BlobStorage> GetBlobStorage(uint32_t cf_id) {
    auto it = column_families_.find(cf_id);
    if (it != column_families_.end()) {
      return it->second;
    }
    return std::weak_ptr<BlobStorage>();
  }

  // REQUIRES: mutex is held
  void GetObsoleteFiles(std::vector<std::string>* obsolete_files,
                        SequenceNumber oldest_sequence);

  // REQUIRES: mutex is held
  void MarkAllFilesForGC() {
    for (auto& cf : column_families_) {
      cf.second->MarkAllFilesForGC();
    }
  }

 private:
  friend class BlobFileSizeCollectorTest;
  friend class VersionTest;

  Status Recover();

  Status OpenManifest(uint64_t number);

  Status WriteSnapshot(log::Writer* log);

  Status Apply(VersionEdit* edit);

  std::string dirname_;
  Env* env_;
  EnvOptions env_options_;
  TitanDBOptions db_options_;
  std::shared_ptr<Cache> file_cache_;

  std::vector<std::string> obsolete_manifests_;

  // As rocksdb described, `DropColumnFamilies()` only records the drop of the
  // column family specified by ColumnFamilyHandle. The actual data is not
  // deleted until the client calls `delete column_family`, namely
  // `DestroyColumnFamilyHandle()`. We can still continue using the column
  // family if we have outstanding ColumnFamilyHandle pointer. So here record
  // the dropped column family but the handler is not destroyed.
  std::unordered_set<uint32_t> obsolete_columns_;

  std::unordered_map<uint32_t, std::shared_ptr<BlobStorage>> column_families_;
  std::unique_ptr<log::Writer> manifest_;
  std::atomic<uint64_t> next_file_number_{1};
};

}  // namespace titandb
}  // namespace rocksdb

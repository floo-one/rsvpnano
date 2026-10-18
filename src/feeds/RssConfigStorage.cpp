#include "feeds/RssConfigStorage.h"
#include "feeds/FeedOrganization.h"

#include <FS.h>

#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace rss {
    std::expected<Config, std::error_code> load(fs::FS& filesystem) {
        return StorageFiles::readTextFile(filesystem, StoragePaths::kRssConfigPath, kMaxConfigBytes)
            .and_then([](const std::string& content) {
                return decodeToml(content);
            });
    }

    std::expected<void, std::error_code> save(fs::FS& filesystem, Config config) {
        if (auto normalized = normalize(config); !normalized) return normalized;
        return encodeToml(config).and_then([&](const std::string& content) {
            return StorageFiles::ensureDirectory(StoragePaths::kConfigPath).and_then([&] {
                return StorageFiles::writeFileAtomic(filesystem, StoragePaths::kRssConfigPath,
                                                     StoragePaths::kRssConfigTempPath,
                                                     StoragePaths::kRssConfigBackupPath, content).and_then([&]() -> std::expected<void, std::error_code> {
                    auto reconciled = FeedOrganization::reconcile(filesystem, config.feeds);
                    if (!reconciled) return std::unexpected(reconciled.error());
                    return {};
                });
            });
        });
    }

} // namespace rss

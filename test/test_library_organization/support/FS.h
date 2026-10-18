#pragma once
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define FILE_READ "r"
#define FILE_WRITE "w"
namespace fs {
struct PowerLoss {};
struct Node { std::string bytes; bool directory = false; };
// Persistence lives in nodes; handles/counters are disposable on simulated reboot.
struct Disk {
    std::map<std::string, std::shared_ptr<Node>> nodes;
    std::vector<std::string> mutations;
    size_t crashAfter = 0;
    std::function<bool(const std::string&, const std::string&)> fail;
    bool fails(const std::string& op, const std::string& path) {
        if (fail && fail(op, path)) { errno = EIO; return true; }
        return false;
    }
    void changed(const std::string& op) {
        mutations.push_back(op);
        if (crashAfter && mutations.size() == crashAfter) throw PowerLoss{};
    }
};
class File {
    std::shared_ptr<Disk> disk_;
    std::shared_ptr<Node> node_;
    std::string path_;
    size_t position_ = 0;
public:
    File() = default;
    File(std::shared_ptr<Disk> disk, std::shared_ptr<Node> node, std::string path)
        : disk_(disk), node_(node), path_(std::move(path)) {}
    explicit operator bool() const { return bool(node_); }
    bool isDirectory() const { return node_ && node_->directory; }
    size_t size() const { return node_ ? node_->bytes.size() : 0; }
    size_t read(uint8_t* out, size_t length) {
        if (!node_ || disk_->fails("read", path_)) return 0;
        length = std::min(length, node_->bytes.size() - position_);
        std::memcpy(out, node_->bytes.data() + position_, length);
        position_ += length;
        return length;
    }
    size_t write(const uint8_t* input, size_t length) {
        if (!node_ || isDirectory()) return 0;
        if (disk_->fails("write", path_)) length /= 2; // Real partial bytes survive.
        node_->bytes.assign(reinterpret_cast<const char*>(input), length);
        disk_->changed("write:" + path_);
        return length;
    }
    void flush() {
        if (node_) {
            if (disk_->fails("flush", path_)) node_->bytes.resize(node_->bytes.size() / 2);
            disk_->changed("flush:" + path_);
        }
    }
    void close() { node_.reset(); }
};
class FS {
public:
    std::shared_ptr<Disk> disk = std::make_shared<Disk>();
    File open(const char* path, const char* mode = FILE_READ) {
        const bool write = std::string(mode) == FILE_WRITE;
        if (disk->fails(write ? "open-write" : "open-read", path)) return {};
        auto found = disk->nodes.find(path);
        if (found == disk->nodes.end()) {
            if (!write) { errno = ENOENT; return {}; }
            disk->nodes[path] = std::make_shared<Node>();
            disk->changed("create:" + std::string(path));
        } else if (write && !found->second->directory) {
            found->second->bytes.clear();
            disk->changed("truncate:" + std::string(path));
        }
        return File(disk, disk->nodes.at(path), path);
    }
    bool remove(const char* path) {
        if (disk->fails("remove", path)) return false;
        auto found = disk->nodes.find(path);
        if (found == disk->nodes.end()) { errno = ENOENT; return false; }
        if (found->second->directory) { errno = EISDIR; return false; }
        disk->nodes.erase(found);
        disk->changed("remove:" + std::string(path));
        return true;
    }
    bool rename(const char* from, const char* to) {
        if (disk->fails("rename", from)) return false;
        auto found = disk->nodes.find(from);
        if (found == disk->nodes.end()) { errno = ENOENT; return false; }
        if (disk->nodes.contains(to)) { errno = EEXIST; return false; }
        disk->nodes[to] = found->second;
        disk->nodes.erase(found);
        disk->changed("rename:" + std::string(from));
        return true;
    }
    bool mkdir(const char* path) {
        if (disk->nodes.contains(path)) return false;
        disk->nodes[path] = std::make_shared<Node>(Node{"", true});
        return true;
    }
    void put(const std::string& path, std::string bytes) {
        disk->nodes[path] = std::make_shared<Node>(Node{std::move(bytes), false});
    }
    const std::string& bytes(const std::string& path) const { return disk->nodes.at(path)->bytes; }
    bool exists(const std::string& path) const { return disk->nodes.contains(path); }
    void reboot() { disk->fail = {}; disk->crashAfter = 0; disk->mutations.clear(); }
};
}
using fs::File;

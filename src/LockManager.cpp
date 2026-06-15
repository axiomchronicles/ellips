#include "elips/kernel/LockManager.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <utility>

namespace elips {

LockManager::LockManager(const std::string& lock_path, LockMode mode)
    : mode_(mode) {
    fd_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw StorageError{"cannot open lock file: " + lock_path};
    }
    const int op =
        (mode_ == LockMode::exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB;
    if (::flock(fd_, op) != 0) {
        ::close(fd_);
        fd_ = -1;
        if (mode_ == LockMode::exclusive) {
            throw LockConflict{"database is already open by another reader or "
                               "writer: " + lock_path};
        }
        throw LockConflict{"database is already open by a writer: " +
                           lock_path};
    }
}

LockManager::~LockManager() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}

LockManager::LockManager(LockManager&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)),
      mode_(std::exchange(other.mode_, LockMode::exclusive)) {}

}  // namespace elips

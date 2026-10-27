#pragma once
// Two ways to get framed messages out of a file. Both hand the parser a
// pointer to the message type byte and the payload length; only how bytes
// reach memory differs.
//
// The sample files frame each message with a 2-byte big-endian length.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "itch/endian.hpp"

namespace itch {

// read() into a fixed buffer. When fewer bytes are left than the next frame
// needs, the tail is moved to the front and the buffer is refilled with one
// read() call. The syscall therefore lands on whichever message happens to
// straddle a buffer boundary.
class ReadIngest {
public:
    static constexpr const char* name = "read";

    explicit ReadIngest(const std::string& path, size_t buf_size = 64 * 1024)
        : cap_(buf_size) {
        fd_ = path == "-" ? 0 : ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) throw std::runtime_error("open " + path + ": " + std::strerror(errno));
        buf_ = static_cast<uint8_t*>(std::aligned_alloc(64, cap_));
    }
    ~ReadIngest() {
        std::free(buf_);
        if (fd_ > 0) ::close(fd_);
    }
    ReadIngest(const ReadIngest&) = delete;
    ReadIngest& operator=(const ReadIngest&) = delete;

    // Returns the message (type byte first) or nullptr at end of input.
    const uint8_t* next(uint16_t& len) {
        if (end_ - pos_ < 2 && !refill(2)) return nullptr;
        len = be16(buf_ + pos_);
        if (end_ - pos_ < size_t(2) + len && !refill(size_t(2) + len)) return nullptr;
        const uint8_t* m = buf_ + pos_ + 2;
        pos_ += size_t(2) + len;
        return m;
    }

    uint64_t read_calls = 0;

private:
    // Make at least `need` bytes available at pos_. False at EOF.
    __attribute__((noinline)) bool refill(size_t need) {
        size_t have = end_ - pos_;
        std::memmove(buf_, buf_ + pos_, have);
        pos_ = 0;
        end_ = have;
        while (end_ < need) {
            ssize_t r = ::read(fd_, buf_ + end_, cap_ - end_);
            ++read_calls;
            if (r < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(std::string("read: ") + std::strerror(errno));
            }
            if (r == 0) return false;  // EOF; a partial trailing frame is dropped
            end_ += size_t(r);
        }
        return true;
    }

    int fd_ = -1;
    uint8_t* buf_ = nullptr;
    size_t cap_;
    size_t pos_ = 0;
    size_t end_ = 0;
};

// Map the whole file and walk it. No syscalls per message; the kernel faults
// pages in on first touch unless populate is set.
class MmapIngest {
public:
    static constexpr const char* name = "mmap";

    explicit MmapIngest(const std::string& path, bool populate = false) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("open " + path + ": " + std::strerror(errno));
        struct stat st {};
        ::fstat(fd, &st);
        size_ = size_t(st.st_size);
        int flags = MAP_PRIVATE | (populate ? MAP_POPULATE : 0);
        void* p = ::mmap(nullptr, size_, PROT_READ, flags, fd, 0);
        ::close(fd);
        if (p == MAP_FAILED) throw std::runtime_error(std::string("mmap: ") + std::strerror(errno));
        ::madvise(p, size_, MADV_SEQUENTIAL);
        base_ = static_cast<const uint8_t*>(p);
    }
    ~MmapIngest() { ::munmap(const_cast<uint8_t*>(base_), size_); }
    MmapIngest(const MmapIngest&) = delete;
    MmapIngest& operator=(const MmapIngest&) = delete;

    const uint8_t* next(uint16_t& len) {
        if (size_ - pos_ < 2) return nullptr;
        len = be16(base_ + pos_);
        if (size_ - pos_ < size_t(2) + len) return nullptr;
        const uint8_t* m = base_ + pos_ + 2;
        pos_ += size_t(2) + len;
        return m;
    }

    uint64_t read_calls = 0;  // always 0; kept so both types report the same fields

private:
    const uint8_t* base_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
};

}  // namespace itch

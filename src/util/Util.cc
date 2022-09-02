/* @file Util.cc
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2021 Zachary Parker
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <memory>
#include <limits>
#include <ranges>

#include <arpa/inet.h>
#include <fcntl.h>
#include <libgen.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <linux/if_tun.h>

#include <spdlog/spdlog.h>

#include <draft/util/Util.hh>

namespace fs = std::filesystem;

namespace draft::util {

namespace {

struct AddrInfoDeleter
{
    void operator()(struct addrinfo *info) const noexcept
    {
        freeaddrinfo(info);
    }
};

size_t iovOp(int fd, const struct iovec *iovs, size_t iovlen, ssize_t (*op)(int, const struct iovec *, int))
{
    if (iovlen > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("network::iovOp iov length > than int max.");

    if (!iovs)
        throw std::invalid_argument("network::iovOp iov vector is null.");

    IOVec iovLocal(iovs, iovlen);

    auto iov = iovLocal.get();
    auto iovCount = static_cast<int>(iovlen);
    size_t total = 0;

    while (iovCount > 0)
    {
        // skip null iovs.
        while ((!iov->iov_base || !iov->iov_len) && iovCount > 0)
        {
            ++iov;
            --iovCount;
        }

        if (iovCount <= 0)
            break;

        ssize_t len = op(fd, iov, iovCount);

        if (len < 0)
        {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK ||
                errno == EINTR)
            {
                continue;
            }

            throw std::system_error(
                errno, std::system_category(), "network::iovOp::read");
        }

        total += static_cast<size_t>(len);

        while (len > 0)
        {
            const auto rem = iov->iov_len - std::min(iov->iov_len, static_cast<size_t>(len));

            if (rem)
            {
                iov->iov_len = rem;
                iov->iov_base = reinterpret_cast<uint8_t *>(iov->iov_base) + len;
                break;
            }

            len -= static_cast<ssize_t>(iov->iov_len);

            ++iov;
            --iovCount;
        }
    }

    return total;
}

auto tcpAddrInfo(const std::string &host, uint16_t port)
{
    const auto portStr = std::to_string(static_cast<unsigned>(port));

    struct addrinfo *info = nullptr;
    struct addrinfo hint { };
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host.c_str(), portStr.c_str(), &hint, &info))
        throw std::system_error(errno, std::system_category(), "bindTcp: getaddrinfo");

    return std::unique_ptr<struct addrinfo, AddrInfoDeleter>(info);
}

} // namespace

size_t readChunk(int fd, void *data, size_t dlen, size_t fileOffset)
{
    auto buf = reinterpret_cast<uint8_t *>(data);

    for (size_t offset = 0; offset < dlen; )
    {
        auto len = ::pread(fd, buf + offset, dlen - offset, static_cast<off_t>(fileOffset + offset));

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "pread");

        if (!len)
            return offset;

        offset += static_cast<size_t>(len);
    }

    return dlen;
}

size_t writeChunk(int fd, iovec *iov, size_t iovCount)
{
    auto written = size_t{ };

    while (iovCount)
    {
        const auto len = ::writev(fd, iov, iovCount);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "write");

        if (!len)
            break;

        auto ulen = static_cast<size_t>(len);

        written += ulen;

        while (ulen && iovCount)
        {
            const auto adv = std::min(iov->iov_len, ulen);
            iov->iov_base = reinterpret_cast<uint8_t *>(iov->iov_base) + adv;
            iov->iov_len -= adv;

            ulen -= adv;

            if (!iov->iov_len)
            {
                ++iov;
                --iovCount;
            }
        }
    }

    return written;
}

size_t writeChunk(int fd, iovec *iov, size_t iovCount, size_t offset, unsigned flags)
{
    auto written = size_t{ };

    while (iovCount)
    {
        if (offset > static_cast<size_t>(std::numeric_limits<off_t>::max()))
        {
            throw std::range_error("writeChunk offset is out of off_t range.");
        }

        const auto len = ::pwritev2(fd, iov, iovCount, static_cast<off_t>(offset), flags);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "pwritev");

        if (!len)
            break;

        auto ulen = static_cast<size_t>(len);

        offset += ulen;
        written += ulen;

        while (ulen && iovCount)
        {
            const auto adv = std::min(iov->iov_len, ulen);
            iov->iov_base = reinterpret_cast<uint8_t *>(iov->iov_base) + adv;
            iov->iov_len -= adv;

            ulen -= adv;

            if (!iov->iov_len)
            {
                ++iov;
                --iovCount;
            }
        }
    }

    return written;
}

namespace {

FileInfo fileInfo(const std::filesystem::directory_entry &dentry)
{
    auto info = FileInfo{ };
    info.path = dentry.path();

    struct stat st{ };
    if (lstat(info.path.c_str(), &st))
        throw std::system_error(errno, std::system_category(), "fileInfo: stat");

    info.status.mode = st.st_mode;
    info.status.uid = st.st_uid;
    info.status.gid = st.st_gid;
    info.status.dev = st.st_dev;
    info.status.blkSize = st.st_blksize;
    info.status.blkCount = st.st_blocks;
    info.status.size = static_cast<size_t>(st.st_size);

    return info;
}

} // namespace

std::vector<FileInfo> getFileInfo(const std::string &path)
{
    unsigned fileId{ };

    if (!fs::exists(path))
    {
        spdlog::warn("getFileInfo: specified path '{}' does not exist."
            , path);

        return { };
    }

    if (!fs::is_directory(path))
    {
        auto info = fileInfo(fs::directory_entry{path});
        info.id = ++fileId;
        return {std::move(info)};
    }

    auto infos = std::vector<FileInfo>{ };

    for (const auto &dentry : fs::recursive_directory_iterator(path))
    {
        auto info = fileInfo(dentry);

        if (!dentry.is_directory())
            info.id = ++fileId;

        infos.push_back(std::move(info));
    }

    return infos;
}

NetworkTarget parseTarget(const std::string &str)
{
    if (str.empty())
        throw std::invalid_argument("parseTarget");

    unsigned long port{2021};
    auto ipEnd = str.find(':');

    if (ipEnd != std::string::npos && ipEnd + 1 < str.size())
    {
        auto portStr = str.substr(ipEnd + 1);
        auto pos = size_t{ };

        port = std::stoul(portStr, &pos);

        if (ipEnd + 1 + pos != str.size())
        {
            spdlog::error("invalid target string (trailing chars): {}", str);
            std::exit(1);
        }

        if (port > std::numeric_limits<uint16_t>::max())
        {
            spdlog::error("invalid port number: {}", port);
            std::exit(1);
        }
    }

    return {str.substr(0, ipEnd), static_cast<uint16_t>(port)};
}

size_t parseSize(const std::string &str)
{
    size_t pos{ };
    auto sz = std::stoul(str, &pos);

    if (pos != str.size())
        throw std::invalid_argument(fmt::format("size option: {}", str));

    return sz;
}

void createTargetFiles(const std::string &root, const std::vector<FileInfo> &infos)
{
    namespace fs = std::filesystem;

    for (const auto &info : infos)
    {
        if (S_ISDIR(info.status.mode))
            continue;

        auto path = rootedPath(root, info.path, info.targetSuffix);
        spdlog::info("createTargetFiles: create file {}: '{}'", info.id, path.string());

        fs::create_directories(util::dirname(path));

        if (const auto max = static_cast<size_t>(std::numeric_limits<off_t>::max());
            info.status.size > max)
        {
            throw std::runtime_error(fmt::format(
                "createTargetFiles: file '{}' is too large for off_t (limit: {})"
                , info.path
                , max));
        }

        auto fd = ScopedFd{::open(path.c_str(), O_RDWR | O_CREAT, info.status.mode & 0777)};
        if (fd.get() < 0)
            throw std::system_error(errno, std::system_category(), "createTargetFiles: open");

        if (!info.status.size)
            continue;

        // allocate space for this file.
        // this can take a while for large files.
        if (auto stat = posix_fallocate(fd.get(), 0, static_cast<off_t>(info.status.size)))
        {
            spdlog::error("posix fallocate err {}", stat);
            throw std::system_error(stat, std::system_category(), "createTargetFiles: posix_fallocate");
        }
    }
}

std::string dirname(std::string path)
{
    return ::dirname(path.data());
}

std::filesystem::path rootedPath(std::filesystem::path root, std::string path, std::string suffix)
{
    namespace fs = std::filesystem;

    // TODO: proper path rooting.

    path += std::move(suffix);

    root = fs::absolute(root);
    root += '/';
    root += std::move(path);

    return fs::absolute(root);
}

namespace net {

ScopedFd bindTun(const std::string &tun)
{
    if (tun.size() >= IFNAMSIZ)
    {
        throw std::invalid_argument(fmt::format(
            "bindTun: tunnel name too long ({} > {})"
            , tun.size()
            , IFNAMSIZ));
    }

    auto fd = ScopedFd{open("/dev/net/tun", O_RDWR)};

    if (fd.get() < 0)
        throw std::system_error(errno, std::system_category(), "bindTun: open");

    struct ifreq ifr{ };
    ifr.ifr_flags = IFF_TUN;
    std::copy(begin(tun), end(tun), ifr.ifr_name);
    ifr.ifr_name[IFNAMSIZ-1] = '\0';

    if (auto stat = ioctl(fd.get(), TUNSETIFF, static_cast<void *>(&ifr)); stat < 0)
    {
        throw std::system_error(errno, std::system_category(),
            fmt::format("bindTun: ioctl - {}", tun));
    }

    return fd;
}

ScopedFd bindTcp(const std::string &host, uint16_t port, unsigned backlog)
{
    if (backlog > static_cast<unsigned>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("bindTcp backlog: " + std::to_string(backlog));

    auto fd = ScopedFd{ };

    if (host.empty())
    {
        auto addr = sockaddr_in{ };
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        fd = ScopedFd{::socket(addr.sin_family, SOCK_STREAM, 0)};

        if (fd.get() < 0)
            throw std::system_error(errno, std::system_category(), "bindTcp: socket");

        if (::bind(fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)))
            throw std::system_error(errno, std::system_category(), "bindTcp: bind");
    }
    else
    {
        auto info = tcpAddrInfo(host, port);

        fd = ScopedFd{::socket(info->ai_family, info->ai_socktype, info->ai_protocol)};

        if (fd.get() < 0)
            throw std::system_error(errno, std::system_category(), "bindTcp: socket");

        if (::bind(fd.get(), info->ai_addr, info->ai_addrlen))
            throw std::system_error(errno, std::system_category(), "bindTcp: bind");
    }

    if (::listen(fd.get(), static_cast<int>(backlog)))
        throw std::system_error(errno, std::system_category(), "bindTcp: listen");

    return fd;
}

ScopedFd connectTcp(const std::string &host, uint16_t port, int tmoMs)
{
    // set o_nonblock for this socket initially, so we can time-out of the
    // connection operation.
    //
    // we'll try to clear that flag before returning.
    const int flags = tmoMs ? SOCK_NONBLOCK : 0;
    auto fd = ScopedFd{socket(AF_INET, SOCK_STREAM | flags, 0)};

    if (fd.get() < 0)
        throw std::system_error(errno, std::system_category(), "connectTcp: socket");

    struct sockaddr_in addr{ };
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, host.data(), &addr.sin_addr);
    addr.sin_port = htons(port);

    if (::connect(fd.get(), reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        if (errno != EINPROGRESS)
            throw std::system_error(errno, std::system_category(),
                fmt::format("connectTcp: connect {}:{}", host, port));

        // wait for connection or timeout.
        auto pfd = pollfd{fd.get(), POLLOUT, 0};
        const auto stat = poll(&pfd, 1, tmoMs);

        if (stat < 0)
        {
            // don't try to connect again if interrupted - caller can try again
            // if desired.
            if (errno == EINTR)
                return ScopedFd{ };

            // other poll(2) errors are pretty egregious.
            throw std::system_error(errno, std::system_category(), "connectTcp: poll");
        }

        if (!stat || !(pfd.revents & POLLOUT))
            return ScopedFd{ };

        // poll completed - grab status.
        int pollErr{ };
        socklen_t errSize{sizeof(pollErr)};
        if (getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &pollErr, &errSize) < 0)
        {
            throw std::system_error(errno, std::system_category(),
                "network::connectTcp: setsockopt");
        }

        if (pollErr)
        {
            throw std::system_error(pollErr, std::system_category(),
                "network::connectTcp: connect/poll");
        }

        // connection completed.
    }

    // disable non-blocking option before returning.
    setNonBlocking(fd.get(), false);

    return fd;
}

ScopedFd bindUdp(const std::string &host, uint16_t port)
{
    auto fd = ScopedFd{socket(AF_INET, SOCK_DGRAM, 0)};

    if (fd.get() < 0)
        throw std::system_error(errno, std::system_category(), "bindUDP: socket");

    struct sockaddr_in addr{ };
    addr.sin_family = AF_INET;
    if (host.empty())
        addr.sin_addr.s_addr = INADDR_ANY;
    else
        inet_pton(AF_INET, host.data(), &addr.sin_addr);
    addr.sin_port = htons(port);

    if (::bind(fd.get(), reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr)) < 0)
        throw std::system_error(errno, std::system_category(), "bindUDP: bind");

    return fd;
}

ScopedFd connectUdp(const std::string &host, uint16_t port)
{
    auto fd = ScopedFd{socket(AF_INET, SOCK_DGRAM, 0)};

    if (fd.get() < 0)
        throw std::system_error(errno, std::system_category(), "connectUDP: socket");

    struct sockaddr_in addr{ };
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, host.data(), &addr.sin_addr);
    addr.sin_port = htons(port);

    if (::connect(fd.get(), reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr)) < 0)
        throw std::system_error(errno, std::system_category(), "connectUDP: connect");

    return fd;
}

ScopedFd accept(int fd)
{
    if (fd < 0)
        throw std::invalid_argument("network::accept: invalid socket file descriptor.");

    return ScopedFd{::accept(fd, nullptr, nullptr)};
}

void setNonBlocking(int fd, bool on)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        throw std::system_error(errno, std::system_category(), "network::fcntl F_GETFL");

    if (on)
        flags |= O_NONBLOCK;
    else
        flags &= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) < 0)
        throw std::system_error(errno, std::system_category(), "network::fcntl F_SETFL");
}

int udpSendQueueSize(int fd)
{
    int value{ };
    auto stat = ioctl(fd, TIOCOUTQ, &value);

    if (stat < 0)
        throw std::system_error(errno, std::system_category(), "network::ioctl SIOCOUTQ");

    if (value < 0)
    {
        throw std::runtime_error(fmt::format(
            "network::ioctl SIOCOUTQ returned negative value: {}",
            value));
    }

    return static_cast<size_t>(value);
}

void writeAll(int fd, const void *data, size_t len)
{
    struct iovec iov = {const_cast<void *>(data), len};

    return writeAll(fd, &iov, 1);
}

void writeAll(int fd, const struct iovec *iovs, size_t iovlen)
{
    iovOp(fd, iovs, iovlen, ::writev);
}

void readAll(int fd, const void *data, size_t len)
{
    struct iovec iov = {const_cast<void *>(data), len};

    return readAll(fd, &iov, 1);
}

void readAll(int fd, const struct iovec *iovs, size_t iovlen)
{
    iovOp(fd, iovs, iovlen, ::readv);
}

std::string peerName(int fd)
{
    auto addr = sockaddr_storage{ };
    auto addrlen = static_cast<socklen_t>(sizeof(addr));

    if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&addr), &addrlen))
        throw std::system_error(errno, std::system_category(), "getpeername");

    auto host = std::string{ };
    host.resize(256);

    auto port = std::string{ };
    port.resize(6);

    if (auto err = getnameinfo(reinterpret_cast<const struct sockaddr *>(&addr), addrlen,
        host.data(), host.size(),
        port.data(), port.size(),
        NI_NAMEREQD))
    {
        throw std::runtime_error("getnameinfo: " + std::string(gai_strerror(err)));
    }

    auto peer = std::string(host.c_str());
    peer += ':';
    peer += port.c_str();

    return peer;
}

}

std::vector<ScopedFd> connectNetworkTargets(const std::vector<NetworkTarget> &targets)
{
    const auto view = std::views::transform(
        targets,
        [](const NetworkTarget &t) {
            return net::connectTcp(t.ip, t.port);
        });

    return {begin(view), end(view)};
}

std::vector<ScopedFd> bindNetworkTargets(const std::vector<NetworkTarget> &targets)
{
    const auto view = std::views::transform(
        targets,
        [](const NetworkTarget &t) {
            return net::bindTcp(t.ip, t.port);
        });

    return {begin(view), end(view)};
}


}

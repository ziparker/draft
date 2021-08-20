#include "Util.hh"

int main(int argc, char **argv)
{
    using namespace draft;

    namespace fs = std::filesystem;

    if (argc < 2)
        std::exit(1);

    const auto path = std::string{argv[1]};
    const auto fileLen = fs::file_size(path);

    auto fd = util::ScopedFd{open(path.c_str(), O_RDWR)};

    auto mmap = std::make_shared<util::ScopedMMap>(
        util::ScopedMMap::map(nullptr, fileLen, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd.get(), 0));

    uint8_t seq{ };
    size_t blksz{1u << 14};
    for (size_t i = 0; i < fileLen; i += blksz)
        std::memset(mmap->uint8Data(i), seq++, std::min(fileLen - i, blksz));

    return 0;
}

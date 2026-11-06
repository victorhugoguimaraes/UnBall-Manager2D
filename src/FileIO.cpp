#include "FileIO.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <vector>

void writeTextAtomically(const std::filesystem::path& destination, std::string_view text) {
    const auto parent = destination.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::string pattern = destination.string() + ".tmp-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    const int descriptor = ::mkstemp(name.data());
    if (descriptor == -1) throw std::system_error(errno, std::generic_category(), "Criar arquivo temporário");
    bool open = true;
    try {
        while (!text.empty()) {
            const auto written = ::write(descriptor, text.data(), text.size());
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::system_error(errno, std::generic_category(), "Gravar arquivo");
            text.remove_prefix(static_cast<std::size_t>(written));
        }
        if (::fsync(descriptor) == -1) throw std::system_error(errno, std::generic_category(), "Sincronizar arquivo");
        const int closed = ::close(descriptor);
        open = false;
        if (closed == -1) throw std::system_error(errno, std::generic_category(), "Fechar arquivo");
        // On Linux rename is atomic; never delete the original after a failed rename.
        std::filesystem::rename(name.data(), destination);
    } catch (...) {
        if (open) ::close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(name.data(), ignored);
        throw;
    }
}

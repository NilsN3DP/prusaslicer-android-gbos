#include "AndroidSecretStore.hpp"

#include <boost/filesystem.hpp>
#include <cctype>
#include <fstream>
#include <sys/stat.h>

namespace Slic3r::App::Android {

AndroidSecretStore::AndroidSecretStore(const std::string& files_dir) :
    m_dir((boost::filesystem::path(files_dir) / "secrets").string())
{
    boost::system::error_code ec;
    boost::filesystem::create_directories(m_dir, ec);
    ::chmod(m_dir.c_str(), 0700);
}

std::string AndroidSecretStore::path_for(const std::string& opt) const
{
    std::string name;
    for (char c : opt)
        name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return (boost::filesystem::path(m_dir) / name).string();
}

bool AndroidSecretStore::save_secret(const std::string& opt, const std::string& usr, const std::string& psswd)
{
    const std::string path = path_for(opt);
    if (usr.empty() && psswd.empty()) {
        boost::system::error_code ec;
        boost::filesystem::remove(path, ec);
        return true;
    }
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << usr << '\n' << psswd;
    }
    ::chmod(path.c_str(), 0600);
    return true;
}

bool AndroidSecretStore::load_secret(const std::string& opt, std::string& usr, std::string& psswd)
{
    std::ifstream in(path_for(opt), std::ios::binary);
    if (!in)
        return false;
    std::getline(in, usr);
    std::getline(in, psswd, '\0');
    return true;
}

} // namespace Slic3r::App::Android

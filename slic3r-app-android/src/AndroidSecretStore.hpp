#pragma once

#include "Slic3r/Biz/Platform/ISecretStore.hpp"

#include <string>

namespace Slic3r::App::Android {

/// Secrets (Prusa Account tokens, printer passwords) in the app's private storage, which no other
/// app can read. The desktop uses the system keyring for this.
class AndroidSecretStore : public Biz::Platform::ISecretStore
{
public:
    explicit AndroidSecretStore(const std::string& files_dir);

    bool save_secret(const std::string& opt, const std::string& usr, const std::string& psswd) override;
    bool load_secret(const std::string& opt, std::string& usr, std::string& psswd) override;

private:
    std::string path_for(const std::string& opt) const;

    std::string m_dir;
};

} // namespace Slic3r::App::Android

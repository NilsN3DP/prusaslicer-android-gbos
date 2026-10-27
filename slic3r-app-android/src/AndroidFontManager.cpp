#include "AndroidFontManager.hpp"

#include "Slic3r/Biz/Emboss/Emboss.hpp"

#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

namespace Slic3r::App::Android {

const Domain::FontList& AndroidFontManager::get_fonts()
{
    if (m_scanned)
        return m_fonts;
    m_scanned = true;

    namespace fs = boost::filesystem;
    boost::system::error_code ec;
    for (const char* dir : {"/system/fonts", "/product/fonts"}) {
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& path = it->path();
            const std::string ext = path.extension().string();
            if (!boost::iequals(ext, ".ttf") && !boost::iequals(ext, ".otf"))
                continue;
            m_fonts.push_back({path.stem().string(), path.string(), Domain::FontDescriptor::Type::file_path});
        }
    }
    std::sort(m_fonts.begin(), m_fonts.end(),
              [](const Domain::FontDescriptor& a, const Domain::FontDescriptor& b) { return a.name < b.name; });
    return m_fonts;
}

std::unique_ptr<const Domain::FontFile> AndroidFontManager::open(const Domain::FontDescriptor& descriptor)
{
    if (descriptor.path.empty())
        return nullptr;
    return Biz::Emboss::create_font_file(descriptor.path.c_str());
}

Biz::Emboss::IFontManager::Descriptor AndroidFontManager::get_current_descriptor(const Domain::FontDescriptor& other_descriptor)
{
    if (other_descriptor.type == Domain::FontDescriptor::Type::file_path)
        return other_descriptor;

    // A style saved on a desktop (wx font description): pick the installed font with the same name.
    for (const Domain::FontDescriptor& font : get_fonts())
        if (boost::iequals(font.name, other_descriptor.name))
            return font;
    return tl::make_unexpected(ConversionError::AnotherOS);
}

} // namespace Slic3r::App::Android

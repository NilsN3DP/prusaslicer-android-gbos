#pragma once

#include "Slic3r/Biz/Emboss/IFontManager.hpp"

namespace Slic3r::App::Android {

/// Fonts for the Emboss (text) tool: the TrueType / OpenType files Android ships in /system/fonts.
class AndroidFontManager : public Biz::Emboss::IFontManager
{
public:
    const Domain::FontList& get_fonts() override;
    std::unique_ptr<const Domain::FontFile> open(const Domain::FontDescriptor& descriptor) override;
    Descriptor get_current_descriptor(const Domain::FontDescriptor& other_descriptor) override;
    Domain::FontDescriptor::Type get_current_type() const override { return Domain::FontDescriptor::Type::file_path; }

private:
    Domain::FontList m_fonts;
    bool m_scanned{false};
};

} // namespace Slic3r::App::Android

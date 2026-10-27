#include "AndroidDialogManager.hpp"
#include "AndroidBridge.hpp"
#include "AndroidWebView.hpp"

#include "Slic3r/App/Browser/AbstractUploadBrowserLogic.hpp"

#include <SDL.h>

#include "Slic3r/Domain/Preset/PresetTree.hpp" // PresetOrigin, needed by PresetSelectionNames.hpp
#include "Slic3r/Biz/Preset/PresetDiffOperation.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionNames.hpp"
#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::App::Android {

namespace {

// Shows a native message box; returns the index of the pressed button (-1 when dismissed).
int message_box(Uint32 flags, const std::string& title, const std::string& text, const std::vector<std::string>& buttons)
{
    std::vector<SDL_MessageBoxButtonData> data;
    for (size_t i = 0; i < buttons.size(); ++i)
        data.push_back({0, int(i), buttons[i].c_str()});
    if (!data.empty()) {
        data.front().flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        data.back().flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    }
    const SDL_MessageBoxData box{flags, nullptr, title.empty() ? "PrusaSlicer" : title.c_str(), text.c_str(),
                                 int(data.size()), data.data(), nullptr};
    int pressed = -1;
    if (SDL_ShowMessageBox(&box, &pressed) != 0)
        SPDLOG_ERROR("SDL_ShowMessageBox failed: {}", SDL_GetError());
    return pressed;
}

std::string file_name_from_wildcards(const std::string& default_file_name, const std::string& wildcards)
{
    if (!default_file_name.empty() || wildcards.empty())
        return default_file_name;
    // "G-code files (*.gcode)|*.gcode" -> "unnamed.gcode"
    const size_t bar = wildcards.find('|');
    const size_t star = wildcards.find("*.", bar == std::string::npos ? 0 : bar);
    if (star == std::string::npos)
        return "unnamed";
    const size_t end = wildcards.find_first_of(";|", star);
    return "unnamed" + wildcards.substr(star + 1, end == std::string::npos ? std::string::npos : end - star - 1);
}

} // namespace

void AndroidDialogManager::show_yesno_dialog(const std::string& title, const std::string& text, const YesNoCallback& callback)
{
    const bool yes = message_box(SDL_MESSAGEBOX_INFORMATION, title, text, {"Ja", "Nein"}) == 0;
    if (callback)
        callback(yes);
}

void AndroidDialogManager::show_rich_yesno_dialog(
    const std::string& title,
    const std::string& text,
    const std::string& check_text,
    const YesNoCallback& callback,
    const CheckBoxCheckedCallback& checked_callback
)
{
    // No checkbox in a native message box: offer "yes, and remember" as a third button.
    const int pressed = message_box(SDL_MESSAGEBOX_INFORMATION, title, text, {"Ja", "Ja – " + check_text, "Nein"});
    if (checked_callback)
        checked_callback(pressed == 1);
    if (callback)
        callback(pressed == 0 || pressed == 1);
}

void AndroidDialogManager::show_yesnocancel_dialog(
    const std::string& title, const std::string& text, const Button& yes, const Button& no, const Button& cancel
)
{
    const int pressed = message_box(SDL_MESSAGEBOX_INFORMATION, title, text, {yes.first, no.first, cancel.first});
    const Button& chosen = pressed == 0 ? yes : pressed == 1 ? no : cancel;
    if (chosen.second)
        chosen.second();
}

void AndroidDialogManager::show_info_dialog(const std::string& text, const std::string& title, bool)
{
    message_box(SDL_MESSAGEBOX_INFORMATION, title, text, {"OK"});
}

void AndroidDialogManager::show_warning_dialog(const std::string& text, const std::string& title)
{
    message_box(SDL_MESSAGEBOX_WARNING, title, text, {"OK"});
}

void AndroidDialogManager::show_error_dialog(const std::string& text, const std::string& title)
{
    message_box(SDL_MESSAGEBOX_ERROR, title, text, {"OK"});
}

std::optional<Biz::StepLoadDialogResult> AndroidDialogManager::show_load_step_dialog(
    const std::string&, double linear_precision, double angle_precision, bool
)
{
    Biz::StepLoadDialogResult result;
    result.linear_precision = linear_precision;
    result.angle_precision  = angle_precision;
    return result;
}

namespace {

std::string display_value(const Domain::ConfigBox& box, const std::string& key)
{
    const Domain::ConfigItem* item = nullptr;
    if (const auto override = box.overrides.find(key); override && box.overrides.get(key))
        item = &*override;
    else
        item = box.items.find(key);
    if (item == nullptr)
        return "–";
    auto value = Biz::value_as_string(*item);
    if (std::holds_alternative<std::string>(value))
        return std::get<std::string>(value);
    std::string joined;
    for (const std::string& v : std::get<std::vector<std::string>>(value))
        joined += (joined.empty() ? "" : "; ") + v;
    return joined;
}

std::string option_label(const Domain::ConfigBox& box, const std::string& key)
{
    if (const Domain::ConfigItem* item = box.items.find(key)) {
        const Domain::ConfigItemDef& def = item->def();
        std::string label = Biz::_u8(def.full_label.empty() ? def.label : def.full_label);
        // Labels repeat across groups (e.g. "External perimeters" for speed, width, acceleration).
        if (def.option_group != Domain::ConfigItemDef::OptionGroup::Unknown) {
            const std::string group = Biz::_u8(Domain::ConfigItemDef::translate_option_group(def.option_group));
            if (!group.empty() && group != label)
                label = group + " › " + label;
        }
        return label;
    }
    return key;
}

const char* kind_name(Domain::Preset::PresetKind kind)
{
    using Domain::Preset::PresetKind;
    switch (kind) {
    case PresetKind::FdmPrinter: return "Drucker";
    case PresetKind::FdmPrint: return "Druckeinstellungen";
    case PresetKind::FdmToolPrint: return "Werkzeug";
    case PresetKind::FdmMaterial: return "Filament";
    default: return "Voreinstellung";
    }
}

} // namespace

// Like WX::UnsavedChangesDialog: per changed preset the modified options, to transfer (the selected
// ones) into the newly selected preset, or to discard. (Saving is disabled on the desktop as well.)
IDialogManager::PresetsSwitchStates AndroidDialogManager::show_unsaved_changes_dialog(
    const std::string& dialog_name,
    const Domain::ConfigPack& config_original,
    const Domain::ConfigPack& config_selected,
    Domain::ConfigPack* config_new_selected,
    const Biz::Preset::PresetSelectionNames& preset_names,
    const Biz::Preset::PresetSelectionNames&,
    const Biz::Preset::PresetInteractor&,
    bool
)
{
    using Biz::Preset::PresetDiffOperation;
    using Domain::Preset::PresetKind;

    PresetsSwitchStates states;
    if (!std::holds_alternative<Domain::ConfigPackFDM>(config_original)) {
        const int pressed = message_box(SDL_MESSAGEBOX_WARNING, dialog_name,
            "Die aktuellen Voreinstellungen haben ungespeicherte Änderungen.", {"Änderungen verwerfen", "Abbrechen"});
        if (pressed != 0)
            return {};
        states[{PresetKind::SlaPrinter}] = {PresetDiffOperation::Discard, {}, {}};
        states[{PresetKind::SlaPrint}] = {PresetDiffOperation::Discard, {}, {}};
        states[{PresetKind::SlaMaterial, 0}] = {PresetDiffOperation::Discard, {}, {}};
        return states;
    }

    const auto& original = std::get<Domain::ConfigPackFDM>(config_original);
    const auto& selected = std::get<Domain::ConfigPackFDM>(config_selected);

    struct Step
    {
        Biz::Preset::PresetSwitchKindId id;
        std::string name;
        const Domain::ConfigBox* original;
        const Domain::ConfigBox* selected;
    };
    std::vector<Step> steps;
    steps.push_back({{PresetKind::FdmPrinter}, preset_names.printer.name, &original.printer, &selected.printer});
    steps.push_back({{PresetKind::FdmPrint}, preset_names.print.name, &original.print, &selected.print});
    for (size_t i = 0; i < std::min(original.tool.size(), selected.tool.size()); ++i)
        steps.push_back({{PresetKind::FdmToolPrint, i}, i < preset_names.tools.size() ? preset_names.tools[i].name : "",
                         &original.tool[i], &selected.tool[i]});
    for (size_t i = 0; i < std::min(original.filament.size(), selected.filament.size()); ++i)
        steps.push_back({{PresetKind::FdmMaterial, i},
                         i < preset_names.materials.size() ? preset_names.materials[i].name : "",
                         &original.filament[i], &selected.filament[i]});

    for (const Step& step : steps) {
        const std::vector<std::string> keys = step.original->diff_keys(*step.selected);
        if (keys.empty())
            continue;
        std::vector<std::string> lines;
        for (const std::string& key : keys)
            lines.push_back(option_label(*step.selected, key) + ":  " + display_value(*step.original, key) + "  →  " +
                            display_value(*step.selected, key));
        std::string title = std::string(kind_name(step.id.kind)) + (step.id.id && step.id.kind != PresetKind::FdmPrint &&
            step.id.kind != PresetKind::FdmPrinter ? " " + std::to_string(*step.id.id + 1) : "") + ": " + step.name;
        std::vector<bool> checked;
        const int result = diff_dialog(title,
            dialog_name + "\nGeänderte Optionen – ausgewählte können in die neue Voreinstellung übernommen werden:",
            lines, config_new_selected != nullptr, checked);
        if (result < 0)
            return {}; // cancelled
        if (result == 1) {
            states[step.id] = {PresetDiffOperation::Discard, {}, {}};
            continue;
        }
        Biz::Preset::PresetSwitchState state{PresetDiffOperation::Transfer, {}, {}};
        for (size_t k = 0; k < keys.size(); ++k) {
            if (!checked[k])
                continue;
            if (const Domain::ConfigItem* item = step.selected->items.find(keys[k]))
                state.items.insert({keys[k], item->value()});
            if (step.selected->overrides.empty())
                continue;
            if (const auto& override = step.selected->overrides.get(keys[k]))
                state.overrides.insert({keys[k], override->value()});
            else
                state.overrides.insert({keys[k], std::optional<Domain::ConfigValue>{}});
        }
        states[step.id] = std::move(state);
    }
    return states;
}

std::string AndroidDialogManager::show_save_dialog(
    Domain::Preset::PresetKind, const std::string& original_name, const Biz::Preset::PresetInteractor&
)
{
    return input_dialog("Voreinstellung speichern", "Name der neuen Voreinstellung:", original_name).value_or("");
}

IDialogManager::NamesPerKindMap AndroidDialogManager::show_save_print_tool_dialog(
    const NamesPerKindMap& original_names_per_kind, const Biz::Preset::PresetInteractor&
)
{
    NamesPerKindMap result;
    for (const auto& [kind, names] : original_names_per_kind) {
        for (const std::string& name : names) {
            const std::optional<std::string> new_name =
                input_dialog("Voreinstellung speichern", "Name der neuen Voreinstellung:", name);
            if (!new_name)
                return {};
            result[kind].push_back(*new_name);
        }
    }
    return result;
}

void AndroidDialogManager::show_file_dialog(
    FileDialogType dialog_type,
    const std::string& dialog_title,
    const boost::filesystem::path&,
    const std::string& default_file_name,
    const std::string& wildcards,
    const FileCallback& callback
)
{
    std::vector<boost::filesystem::path> paths;
    if (dialog_type == FileDialogType::Save) {
        if (auto path = pick_file_to_save(dialog_title, file_name_from_wildcards(default_file_name, wildcards),
                                          "application/octet-stream"))
            paths.emplace_back(*path);
    } else {
        for (const std::string& p : pick_files_to_open(dialog_title, "*/*", dialog_type == FileDialogType::OpenMultiple))
            paths.emplace_back(p);
    }
    if (callback)
        callback(!paths.empty(), paths);
}

std::string AndroidDialogManager::show_input_dialog(const std::string& title, const std::string& text, const std::string& default_value)
{
    return input_dialog(title, text, default_value).value_or("");
}

void AndroidDialogManager::show_input_dialog_with_buttons(
    const std::string& title,
    const std::string& text,
    const std::string& default_value,
    const std::vector<ButtonWithCallback>& buttons
)
{
    const std::optional<std::string> value = input_dialog(title, text, default_value);
    if (!value || buttons.empty())
        return;
    std::vector<std::string> labels;
    for (const ButtonWithCallback& b : buttons)
        labels.push_back(b.text);
    const int index = buttons.size() == 1 ? 0 : choice_dialog(title, labels);
    if (index >= 0 && buttons[size_t(index)].callback)
        buttons[size_t(index)].callback(*value);
}

std::string AndroidDialogManager::show_combo_dialog(const std::string& title, const std::string&, const std::vector<std::string>& values)
{
    const int index = choice_dialog(title, values);
    return index >= 0 ? values[size_t(index)] : std::string();
}

void AndroidDialogManager::show_webview_dialog(
    std::unique_ptr<Browser::AbstractBrowserLogic>&& logic, Slic3r::Biz::ProjectInteractor* project_interactor
)
{
    // Non-blocking, unlike the desktop's ShowModal(): the callers do not wait for a result.
    WebViewManager::instance().open(WebViewPlace::Dialog, std::move(logic), project_interactor);
}

void AndroidDialogManager::show_upload_webview_dialog(
    std::unique_ptr<Browser::AbstractUploadBrowserLogic>&& logic,
    Slic3r::Biz::ProjectInteractor* project_interactor,
    const UploadCallback& callback
)
{
    // e.g. "send to Connect": the page picks the printer, the logic holds the result.
    WebViewManager::instance().open(WebViewPlace::Dialog, std::move(logic), project_interactor,
        [callback](bool ok, Browser::AbstractBrowserLogic& base) {
            auto& upload = static_cast<Browser::AbstractUploadBrowserLogic&>(base);
            if (callback)
                callback(ok && upload.success(), upload.result_data());
        });
}

// Simplified WX::DiffDialog: two printer presets, or two print presets of the selected printer.
void AndroidDialogManager::show_diff_dialog(const Biz::Preset::PresetInteractor& presets, std::optional<Domain::Preset::PresetKind>)
{
    const int what = choice_dialog("Voreinstellungen vergleichen", {"Drucker", "Druckeinstellungen"});
    if (what < 0)
        return;

    std::vector<std::string> names;
    std::vector<const Domain::ConfigBox*> boxes;
    const Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
    if (what == 0) {
        const auto& printers = presets.printer_presets().items();
        for (size_t i = 0; i < printers.size(); ++i) {
            const auto& item = printers.at(i);
            const auto [ref, runtime] = presets.get_printer_preset(item.hw_printer_config_id, item.id);
            if (ref.get().kind != Domain::Preset::PresetKind::FdmPrinter)
                continue;
            names.push_back(item.ui_hw_config_name() + " – " + item.ui_preset_name());
            boxes.push_back(&ref.get().config_box());
        }
    } else {
        for (const auto [ref, runtime] : presets.get_print_presets(selected.hw_config.id, selected.printer.id)) {
            names.push_back(ref.get().name);
            boxes.push_back(&ref.get().config_box());
        }
    }
    if (names.size() < 2) {
        show_info_dialog("Es gibt nichts zu vergleichen.", "Voreinstellungen vergleichen", false);
        return;
    }
    const int left = choice_dialog("Erste Voreinstellung", names);
    if (left < 0)
        return;
    const int right = choice_dialog("Vergleichen mit", names);
    if (right < 0)
        return;

    const Domain::ConfigBox& a = *boxes[left];
    const Domain::ConfigBox& b = *boxes[right];
    std::vector<std::string> lines;
    for (const std::string& key : a.diff_keys(b))
        lines.push_back(option_label(a, key) + ":  " + display_value(a, key) + "  →  " + display_value(b, key));
    if (lines.empty()) {
        show_info_dialog("Die beiden Voreinstellungen sind gleich.", "Voreinstellungen vergleichen", false);
        return;
    }
    choice_dialog(names[left] + "  →  " + names[right] + "  (" + std::to_string(lines.size()) + " Unterschiede)", lines);
}

std::string AndroidDialogManager::show_ramming_dialog(const std::string& ramming_parameters)
{
    // TODO: graphical ramming editor; the raw parameter string is editable meanwhile.
    return input_dialog("Ramming", "Ramming-Parameter:", ramming_parameters).value_or(ramming_parameters);
}

void AndroidDialogManager::open_in_browser(const std::string& link, int)
{
    open_url(link);
}

} // namespace Slic3r::App::Android

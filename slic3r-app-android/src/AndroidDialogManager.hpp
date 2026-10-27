#pragma once

#include "Slic3r/App/IDialogManager.hpp"

namespace Slic3r::App::Android {

/// IDialogManager on Android: native AlertDialogs (via SDL / JNI) and the system file picker.
class AndroidDialogManager : public IDialogManager
{
public:
    // IMessageDialogProvider
    void show_yesno_dialog(const std::string& title, const std::string& text, const YesNoCallback& callback) override;
    void show_rich_yesno_dialog(
        const std::string& title,
        const std::string& text,
        const std::string& check_text,
        const YesNoCallback& callback,
        const CheckBoxCheckedCallback& checked_callback
    ) override;
    void show_yesnocancel_dialog(
        const std::string& title, const std::string& text, const Button& yes, const Button& no, const Button& cancel
    ) override;
    void show_info_dialog(const std::string& text, const std::string& title, bool is_marked) override;
    void show_warning_dialog(const std::string& text, const std::string& title) override;
    void show_error_dialog(const std::string& text, const std::string& title) override;
    std::optional<Biz::StepLoadDialogResult> show_load_step_dialog(
        const std::string& filename, double linear_precision, double angle_precision, bool multiple
    ) override;

    // IPresetDialogManager
    PresetsSwitchStates show_unsaved_changes_dialog(
        const std::string& dialog_name,
        const Domain::ConfigPack& config_original,
        const Domain::ConfigPack& config_selected,
        Domain::ConfigPack* config_new_selected,
        const Biz::Preset::PresetSelectionNames& preset_names,
        const Biz::Preset::PresetSelectionNames& preset_names_new,
        const Biz::Preset::PresetInteractor& preset_interactor,
        bool new_printer_has_multiple_extruders
    ) override;
    std::string show_save_dialog(
        Domain::Preset::PresetKind kind,
        const std::string& original_name,
        const Biz::Preset::PresetInteractor& preset_interactor
    ) override;
    NamesPerKindMap show_save_print_tool_dialog(
        const NamesPerKindMap& original_names_per_kind, const Biz::Preset::PresetInteractor& preset_interactor
    ) override;

    // IDialogManager
    void show_file_dialog(
        FileDialogType dialog_type,
        const std::string& dialog_title,
        const boost::filesystem::path& default_folder,
        const std::string& default_file_name,
        const std::string& wildcards,
        const FileCallback& callback
    ) override;
    std::string show_input_dialog(const std::string& title, const std::string& text, const std::string& default_value) override;
    void show_input_dialog_with_buttons(
        const std::string& title,
        const std::string& text,
        const std::string& default_value,
        const std::vector<ButtonWithCallback>& buttons
    ) override;
    std::string show_combo_dialog(const std::string& title, const std::string& text, const std::vector<std::string>& values) override;
    void show_webview_dialog(
        std::unique_ptr<Browser::AbstractBrowserLogic>&& logic, Slic3r::Biz::ProjectInteractor* project_interactor
    ) override;
    void show_upload_webview_dialog(
        std::unique_ptr<Browser::AbstractUploadBrowserLogic>&& logic,
        Slic3r::Biz::ProjectInteractor* project_interactor,
        const UploadCallback& callback
    ) override;
    void show_diff_dialog(
        const Slic3r::Biz::Preset::PresetInteractor& preset_interactor, std::optional<Domain::Preset::PresetKind> kind
    ) override;
    std::string show_ramming_dialog(const std::string& ramming_parameters) override;
    void open_in_browser(const std::string& link, int flag) override;
};

} // namespace Slic3r::App::Android

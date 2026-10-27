#pragma once

// Calls into the Java side (com.prusa3d.slicer.PrusaSlicerActivity). All functions block the
// calling (SDL) thread until the user has answered; the Java side runs the UI on the UI thread.

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App::Android {

/// Opens the system file picker. Returns real filesystem paths (empty when cancelled).
std::vector<std::string> pick_files_to_open(const std::string& title, const std::string& mime_types, bool multiple);

/// Opens the system "save as" picker. Returns the filesystem path of the created file.
std::optional<std::string> pick_file_to_save(const std::string& title, const std::string& suggested_name, const std::string& mime_type);

/// Text input dialog. Returns std::nullopt when cancelled.
std::optional<std::string> input_dialog(const std::string& title, const std::string& text, const std::string& default_value);

/// Single choice dialog. Returns the chosen index or -1 when cancelled.
int choice_dialog(const std::string& title, const std::vector<std::string>& values);

/// Changed options with check boxes; buttons Transfer (when `can_transfer`), Discard, Cancel.
/// Returns 0 = transfer (`checked` holds the selection), 1 = discard, -1 = cancel.
int diff_dialog(const std::string& title, const std::string& message, const std::vector<std::string>& lines,
                bool can_transfer, std::vector<bool>& checked);

/// Opens a URL in the default browser.
void open_url(const std::string& url);

/// Opens a folder in the system file manager.
void open_folder(const std::string& path);

/// Highlights / clears the cog of the side bar (the preferences dialog opened or closed).
void set_side_bar_preferences_selected(bool selected);
/// Preferences "Log in Downloads speichern" -> PrusaSlicerActivity.setLogExport.
void set_log_export(bool enabled);

/// Directory for app data (PrusaSlicer's datadir).
std::string files_dir();

} // namespace Slic3r::App::Android

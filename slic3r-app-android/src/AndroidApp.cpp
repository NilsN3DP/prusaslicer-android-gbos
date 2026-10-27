// PrusaSlicer for Android: the counterpart of slic3r-app-launcher + slic3r-app-desktop.
// Same services and render modules as the desktop app, hosted in an SDL window instead of wx.

#include "AndroidBridge.hpp"
#include "AndroidDialogManager.hpp"
#include "AndroidFontManager.hpp"
#include "AndroidSecretStore.hpp"
#include "AndroidWebView.hpp"

#include <Slic3r/App/Browser/BrowserLogicConnectPage.hpp>
#include <Slic3r/App/Browser/BrowserLogicLogInRedirect.hpp>
#include <Slic3r/App/Browser/BrowserLogicPrintables.hpp>

#include <SDL.h>
#include <imgui/imgui.h>
#include <jni.h>

#include <Slic3r/App/AppConfig.hpp>
#include <Slic3r/App/AppConfigInteractor.hpp>
#include <Slic3r/App/AppConfigProvider.hpp>
#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/Init.hpp>
#include <Slic3r/App/LoadingRenderModule.hpp>
#include <Slic3r/App/Localization.hpp>
#include <Slic3r/App/Navigator.hpp>
#include <Slic3r/App/Plater/PlaterRenderModule.hpp>
#include <Slic3r/App/Plater/ThumbnailImageGenerator.hpp>
#include <Slic3r/App/Platform/IFileExplorerHandler.hpp>
#include <Slic3r/App/Platform/SDL/SDLMainThreadDispatcher.hpp>
#include <Slic3r/App/Platform/SDL/SDLRenderCanvas.hpp>
#include <Slic3r/App/PopNotification/PopNotificationCenter.hpp>
#include <Slic3r/App/PresetUpdater/PresetUpdaterController.hpp>
#include <Slic3r/App/Preview/PreviewRenderModule.hpp>
#include <Slic3r/App/PrusaLinkStorageListener.hpp>
#include <Slic3r/App/ProjectSaver.hpp>
#include <Slic3r/App/Render/TextureManager.hpp>
#include <Slic3r/App/ResourceResolver.hpp>
#include <Slic3r/App/Theme.hpp>
#include <Slic3r/App/ThumbnailStore.hpp>
#include <Slic3r/App/ThumbnailStoreUpdater.hpp>
#include <Slic3r/App/Undo/Store.hpp>
#include <Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp>
#include <Slic3r/Biz/FileLoadingLogic.hpp>
#include <Slic3r/Biz/Platform/JobManager/JobManager.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include <Slic3r/Biz/Platform/Termination.hpp>
#include <Slic3r/Biz/Preset/IO/BundlePaths.hpp>
#include <Slic3r/Biz/ProjectInteractor.hpp>
#include <Slic3r/Biz/Scene/BedGeometry.hpp>
#include <Slic3r/Biz/SecretStoreDummy.hpp>
#include <Slic3r/Biz/UserAccount/UserAccountInteractor.hpp>
#include <Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp>
#include <Slic3r/Directories.hpp>
#include <Slic3r/Domain/Workbench.hpp>
#include <Slic3r/Log.hpp>

#include <boost/filesystem.hpp>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <utility>

namespace Slic3r::App::Android {

namespace {

class AndroidFileExplorerHandler : public Platform::IFileExplorerHandler
{
public:
    void open_folder(const boost::filesystem::path& path) override { Android::open_folder(path.string()); }
    void open_datadir_folder() override { Android::open_folder(data_dir()); }
};

class AndroidApp;
AndroidApp* g_app = nullptr; ///< for the JNI entry points of the side bar

// Links can arrive (app started from a prusaslicer:// link) before the dispatcher exists.
std::mutex g_early_links_mutex;
std::vector<std::string> g_early_links;
std::atomic<bool> g_dispatcher_ready{false};

class AndroidApp
{
public:
    explicit AndroidApp(SDL_Window* window) : m_window(window) { g_app = this; }
    ~AndroidApp();

    void init();
    void run();

    /// Side bar cog: same behaviour as MainFrame::init_preferences_button on the desktop.
    void toggle_preferences()
    {
        if (!m_init_completed)
            return;
        // Navigator::current_modal_dialog() is never updated: track the dialog via the callback.
        const ModalDialog current = m_modal_dialog;
        if (current == ModalDialog::Preferences)
            m_navigator.set_modal_dialog(ModalDialog::None);
        else if (current == ModalDialog::None)
            m_navigator.set_modal_dialog(ModalDialog::Preferences);
    }

    /// Side bar Connect / Printables tab: PrusaSlicer's own page logic in an Android WebView.
    void open_tab(WebViewPlace place)
    {
        if (!m_init_completed || WebViewManager::instance().has_tab(place))
            return;
        std::unique_ptr<Browser::AbstractBrowserLogic> logic;
        if (place == WebViewPlace::Connect)
            logic = std::make_unique<Browser::BrowserLogicConnectPage>(*m_project_interactor);
        else if (place == WebViewPlace::Printables)
            logic = std::make_unique<Browser::BrowserLogicPrintables>(*m_project_interactor);
        if (logic)
            WebViewManager::instance().open(place, std::move(logic), m_project_interactor.get());
    }

    /// Side bar account button: like the desktop's account menu, sign in or out.
    void account_button()
    {
        if (!m_init_completed)
            return;
        auto& account = m_project_interactor->user_account_interactor();
        if (account.is_logged_in()) {
            AppServices::instance().dialog_manager().show_yesno_dialog(
                "Prusa Account", "Als " + account.username() + " abmelden?", [this](bool yes) {
                    if (yes)
                        m_project_interactor->user_account_interactor().do_log_out();
                });
            return;
        }
        if (!AppServices::instance().app_config().is_prusa_account_enabled()) {
            AppServices::instance().dialog_manager().show_info_dialog(
                "Der Prusa Account ist in den Einstellungen (Zahnrad → Services) ausgeschaltet.", "Prusa Account", false);
            return;
        }
        AppServices::instance().dialog_manager().show_webview_dialog(
            std::make_unique<Browser::BrowserLogicLogInRedirect>(account), m_project_interactor.get());
    }

    /// prusaslicer:// links (Printables "Open in PrusaSlicer", Prusa Account login redirect).
    /// Kept until the project is set up, like the desktop handles them at the end of finish_init().
    void open_link(const std::string& url)
    {
        if (!m_init_completed) {
            m_pending_links.push_back(url);
            return;
        }
        if (url.rfind("prusaslicer://login", 0) == 0)
            m_project_interactor->on_login_data(url);
        else if (url.rfind("prusaslicer://open", 0) == 0)
            m_project_interactor->on_download_models({url});
        else if (url.rfind("file://", 0) == 0)
            open_files({url.substr(7)});
        else
            SPDLOG_WARN("Unhandled link {}", url);
    }

    /// Files opened from other apps (Files "open with", drag & drop), like MainFrameDropTarget.
    void open_files(const std::vector<std::string>& files)
    {
        if (!m_navigator.has_modules())
            return;
        std::vector<boost::filesystem::path> paths;
        for (const std::string& f : files)
            if (Biz::FileLoadingLogic::is_supported_file(f))
                paths.emplace_back(f);
        if (paths.empty()) {
            AppServices::instance().dialog_manager().show_info_dialog(
                "Dieses Dateiformat kann PrusaSlicer nicht öffnen.", "PrusaSlicer", false);
            return;
        }
        if (paths.size() == 1 && Biz::FileLoadingLogic::is_project_file(paths.front().string()))
            m_project_interactor->load_project(paths.front());
        else {
            m_project_interactor->load_models_to_project(std::move(paths));
            m_navigator.navigate_to_module_type(Render::ModuleType::Plater);
        }
    }

private:
    std::vector<std::string> m_pending_links;
    void finish_init();
    void on_preset_updater_forced_state(bool has_forced);
    void init_translations();

    SDL_Window* m_window;
    std::unique_ptr<Platform::SDL::SDLRenderCanvas> m_canvas;
    std::unique_ptr<Biz::ProjectInteractor> m_project_interactor;
    std::unique_ptr<LoadingRenderModule> m_loading_module;
    std::unique_ptr<Plater::PlaterRenderModule> m_plater_module;
    std::unique_ptr<Preview::PreviewRenderModule> m_preview_module;
    Domain::Workbench m_workbench;
    Navigator m_navigator;
    ModalDialog m_modal_dialog{ModalDialog::None};
    Undo::Store* m_undo_store{nullptr};
    std::unique_ptr<PrintHost::PrusaLinkStorageListener> m_prusalink_storage_listener;
    std::shared_ptr<Plater::ThumbnailImageGenerator> m_thumbnail_image_generator;
    std::shared_ptr<App::ThumbnailStore> m_thumbnail_store;
    std::shared_ptr<App::ThumbnailStoreUpdater> m_thumbnail_store_updater;
    std::shared_ptr<ProjectSaver> m_project_saver;
    Biz::Preset::IO::BundlePaths m_bundle_paths;
    bool m_init_completed{false};
};

void AndroidApp::init_translations()
{
    AppConfig& app_config = AppServices::instance().app_config();
    std::string language = app_config.get<std::string>("translation_language");
    if (!localization().set_language(language))
        SPDLOG_ERROR("Switching to language {} failed, using English", language);
}

void AndroidApp::init()
{
    auto& app_services = AppServices::instance();
    app_services.set_theme(std::make_unique<Theme>(app_services.app_config().get<Theme::Style>("theme")));

    using Biz::Platform::PlatformServices;
    using Biz::Platform::JobManager::JobManager;
    auto& platform_services = PlatformServices::instance();
    init_translations();

    platform_services.set_main_thread_dispatcher(std::make_unique<Platform::SDL::SDLMainThreadDispatcher>());
    {
        std::lock_guard lock(g_early_links_mutex);
        m_pending_links = std::exchange(g_early_links, {});
        g_dispatcher_ready = true;
    }
    // Keeps the Prusa Account login across restarts (app-private storage).
    platform_services.set_secret_store(std::make_unique<AndroidSecretStore>(files_dir()));
    if (!app_services.app_config().is_prusa_account_enabled())
        Biz::UserAccount::TokenStore::reset();
    platform_services.set_job_manager(std::make_unique<JobManager>(platform_services.main_thread_dispatcher()));
    platform_services.set_app_config_provider(std::make_unique<AppConfigProvider>());
    platform_services.set_app_instance_message_handler(
        Biz::AppInstance::create_app_instance_message_handler(platform_services.main_thread_dispatcher())
    );

    m_thumbnail_image_generator = std::make_shared<Plater::ThumbnailImageGenerator>();
    m_project_interactor = std::make_unique<Biz::ProjectInteractor>(
        m_workbench, platform_services.main_thread_dispatcher(), *m_thumbnail_image_generator
    );
    auto undo_store_ptr = std::make_unique<Undo::Store>(*m_project_interactor);
    m_undo_store = undo_store_ptr.get();
    m_project_interactor->set_undo_provider(std::move(undo_store_ptr));

    m_thumbnail_store = std::make_shared<App::ThumbnailStore>(*m_project_interactor);
    m_thumbnail_store_updater =
        std::make_shared<App::ThumbnailStoreUpdater>(*m_thumbnail_image_generator, m_thumbnail_store);

    app_services.set_dialog_manager(std::make_unique<AndroidDialogManager>());
    app_services.set_pop_notification_center(
        std::make_unique<PopNotification::PopNotificationCenter>(*m_project_interactor)
    );
    app_services.set_file_explorer_handler(std::make_unique<AndroidFileExplorerHandler>());
    app_services.file_explorer_handler().add_listener<Platform::IFileExplorerErrorListener>(
        &app_services.pop_notification_center()
    );
    platform_services.job_manager().add_listener<Biz::Platform::JobManager::IJobManagerStatusChangedListener>(
        &app_services.pop_notification_center()
    );
    m_project_interactor->user_account_interactor().add_listener<Biz::UserAccount::IUserAccountListener>(
        &app_services.pop_notification_center()
    );
    m_project_interactor->connect_message_handler().add_listener<Biz::Connect::IConnectHandlerListener>(
        &app_services.pop_notification_center()
    );

    PresetUpdater::ControllerEnvironment preset_updater_environment;
    preset_updater_environment.timer_queue = &platform_services.timer_queue();
    preset_updater_environment.online_allowed = []() {
        return AppServices::instance().app_config().get<bool>("enable_preset_update");
    };
    preset_updater_environment.request_render = []() {
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    };
    app_services.set_preset_updater_controller(std::make_unique<PresetUpdater::PresetUpdaterController>(
        m_project_interactor->preset_updater_interactor(), std::move(preset_updater_environment)
    ));

    PresetUpdater::PresetUpdaterController& preset_updater_controller = app_services.preset_updater_controller();
    preset_updater_controller.set_forced_state_callback([this](bool has_forced) {
        on_preset_updater_forced_state(has_forced);
    });
    preset_updater_controller.set_show_dialog_callback([this]() {
        if (m_init_completed)
            m_navigator.set_modal_dialog(ModalDialog::PresetUpdater);
        else
            m_loading_module->set_opened_preset_updater(true);
    });
    preset_updater_controller.set_presets_installed_callback([this]() {
        if (m_init_completed) {
            m_project_interactor->preset_interactor().load_preset_bundle(m_bundle_paths);
            return;
        }
        AppServices::instance().preset_updater_controller().start();
    });

    m_loading_module = std::make_unique<LoadingRenderModule>(preset_updater_controller, m_navigator);
    m_project_saver = std::make_shared<ProjectSaver>(*m_project_interactor, *m_thumbnail_store);

    m_canvas = std::make_unique<Platform::SDL::SDLRenderCanvas>(m_window);
    platform_services.app_instance_message_handler().init(nullptr);
    platform_services.set_render_request_handler(m_canvas.get());

    preset_updater_controller.start();
    m_navigator.set_canvas(*m_canvas);
    m_canvas->set_render_module(m_loading_module.get());
    m_canvas->request_render();
}

void AndroidApp::on_preset_updater_forced_state(bool has_forced)
{
    if (!has_forced) {
        finish_init();
        return;
    }
    if (!m_init_completed)
        m_loading_module->set_opened_preset_updater(true);
}

void AndroidApp::finish_init()
{
    if (m_init_completed)
        return;
    m_init_completed = true;

    auto& app_services = AppServices::instance();
    auto& platform_services = Biz::Platform::PlatformServices::instance();
    auto& preset_interactor = m_project_interactor->preset_interactor();

    preset_interactor.print_tool_cbi().set_app_config_cbol(
        app_services.app_config_interactor().app_config_cbi().config_box_list()
    );
    m_bundle_paths = Biz::Preset::IO::BundlePaths::make_standard_runtime();
    preset_interactor.load_preset_bundle(m_bundle_paths);

    m_plater_module = std::make_unique<Plater::PlaterRenderModule>(
        m_workbench, *m_project_interactor, *m_undo_store, m_thumbnail_store, m_thumbnail_store_updater,
        m_thumbnail_image_generator, std::make_unique<AndroidFontManager>(), m_project_saver
    );
    m_preview_module = std::make_unique<Preview::PreviewRenderModule>(
        m_workbench, *m_project_interactor, *m_undo_store, m_thumbnail_store, m_thumbnail_store_updater,
        m_thumbnail_image_generator, m_plater_module.get(), &m_plater_module->plugin_system(), m_project_saver
    );

    m_project_interactor->removable_drive_service().add_status_listener(&app_services.pop_notification_center());
    m_project_interactor->set_dialog_provider(&app_services.dialog_manager());
    preset_interactor.set_dialog_manager(&app_services.dialog_manager());
    m_project_interactor->new_project();

    m_navigator.on_init(*m_plater_module, *m_preview_module, *m_canvas, m_project_interactor.get());
    m_navigator.callbacks().modal_dialog_changed = [this](ModalDialog dialog) {
        m_modal_dialog = dialog;
        set_side_bar_preferences_selected(dialog == ModalDialog::Preferences);
        if (dialog == ModalDialog::None)
            set_log_export(AppServices::instance().app_config().get<bool>("android_log_export"));
    };
    set_log_export(AppServices::instance().app_config().get<bool>("android_log_export"));
    m_loading_module->set_opened_preset_updater(false);

    m_canvas->set_next_render_module(m_plater_module.get());
    m_canvas->render();
    m_canvas->request_render();
    if (m_navigator.callbacks().render_module_switched)
        m_navigator.callbacks().render_module_switched();

    m_project_interactor->user_account_interactor().init(app_services.app_config().is_prusa_account_enabled());

    m_prusalink_storage_listener = std::make_unique<PrintHost::PrusaLinkStorageListener>(*m_project_interactor);
    platform_services.job_manager().add_listener<Biz::Platform::JobManager::IJobManagerStatusChangedListener>(
        m_prusalink_storage_listener.get()
    );
    app_services.pop_notification_center().set_open_invalid_data_dialog_fn(
        std::bind(&Navigator::open_invalid_data_dialog, &m_navigator)
    );
    m_plater_module->plugin_system().add_listener<Lua::IPluginInstallationListener>(
        &app_services.pop_notification_center()
    );

    m_project_interactor->backup_store().start_crash_detection();
    for (const std::string& url : std::exchange(m_pending_links, {}))
        open_link(url);
    app_services.preset_updater_controller().start_background_check();
}

void AndroidApp::run()
{
    while (!m_canvas->quit_requested()) {
        SDL_Event event;
        // Block until something happens; render requests push a wake-up event.
        if (SDL_WaitEventTimeout(&event, 100)) {
            do {
                m_canvas->handle_event(event);
            } while (SDL_PollEvent(&event));
        }
        m_canvas->idle();
    }
}

AndroidApp::~AndroidApp()
{
    AppServices& app_services = AppServices::instance();
    if (app_services.has_preset_updater_controller())
        app_services.preset_updater_controller().shutdown();
    Biz::Platform::close();
    flush_logs();
}

/// libcurl (OpenSSL) knows no CA store on Android: bundle the system certificates into one PEM
/// file and hand it over through SSL_CERT_FILE (read by HttpCurl, see apply-patches.sh).
void init_ca_bundle(const std::string& files)
{
    namespace fs = boost::filesystem;
    const fs::path bundle = fs::path(files) / "cacert.pem";
    boost::system::error_code ec;
    if (!fs::exists(bundle, ec) || fs::file_size(bundle, ec) == 0) {
        std::ofstream out(bundle.string(), std::ios::binary | std::ios::trunc);
        // Android 14+ ships the updatable store in the Conscrypt APEX.
        for (const char* dir : {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                std::ifstream in(it->path().string(), std::ios::binary);
                out << in.rdbuf() << '\n';
            }
            if (out.tellp() > 0)
                break;
        }
    }
    ::setenv("SSL_CERT_FILE", bundle.string().c_str(), 1);
}

/// PrusaSlicer's resources are unpacked from the APK assets by the Java side.
void init_android_paths(InitParams& init_params)
{
    const std::string files = files_dir();
    init_ca_bundle(files);
    ::setenv("HOME", files.c_str(), 1);
    ::setenv("XDG_CONFIG_HOME", (files + "/config").c_str(), 1);
    ::setenv("XDG_CACHE_HOME", (files + "/cache").c_str(), 1);
    // Without TMPDIR boost::filesystem::temp_directory_path() falls back to /data/local/tmp, which
    // apps may not write to (the preset updater then fails and its error path crashes on startup).
    const std::string tmp = files + "/tmp";
    boost::system::error_code ec;
    boost::filesystem::create_directories(tmp, ec);
    ::setenv("TMPDIR", tmp.c_str(), 1);

    const boost::filesystem::path resources = boost::filesystem::path(files) / "resources";
    set_resources_dir(resources.string());
    set_var_dir((resources / "icons").string());
    set_local_dir((resources / "localization").string());
    set_sys_shapes_dir((resources / "shapes").string());
    set_custom_gcodes_dir((resources / "custom_gcodes").string());

    init_params.misc.datadir = (boost::filesystem::path(files) / "PrusaSlicer").string();
}

} // namespace

} // namespace Slic3r::App::Android

// Side bar (PrusaSlicerActivity, UI thread) -> PrusaSlicer (SDL thread)
/// Touchpad two-finger swipe (PrusaSlicerActivity): over an ImGui window (filament / printer lists,
/// menus) it scrolls like the mouse wheel, over the 3D scene it pans like a middle-button drag.
/// phase: 0 = begin, 1 = move, 2 = end; x, y: position in the surface, dx, dy: finger movement.
extern "C" JNIEXPORT void JNICALL Java_com_prusa3d_slicer_PrusaSlicerActivity_nativeTouchpadSwipe(
    JNIEnv*, jclass, jint phase, jfloat x, jfloat y, jfloat dx, jfloat dy)
{
    Slic3r::Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread(
        [phase, x, y, dx, dy]() {
            static bool s_scrolling = false;
            static bool s_panning = false;
            static float s_x = 0.f, s_y = 0.f;
            auto push_motion = [](float px, float py, Uint32 state) {
                SDL_Event e{};
                e.type = SDL_MOUSEMOTION;
                e.motion.x = int(px);
                e.motion.y = int(py);
                e.motion.state = state;
                SDL_PushEvent(&e);
            };
            auto push_button = [](float px, float py, bool down) {
                SDL_Event e{};
                e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                e.button.button = SDL_BUTTON_MIDDLE;
                e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
                e.button.clicks = 1;
                e.button.x = int(px);
                e.button.y = int(py);
                SDL_PushEvent(&e);
            };
            if (phase == 0 || (!s_scrolling && !s_panning)) {
                s_x = x;
                s_y = y;
                s_scrolling = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
                s_panning = !s_scrolling;
                push_motion(s_x, s_y, 0);
                if (s_panning)
                    push_button(s_x, s_y, true);
            }
            if (phase == 1) {
                if (s_scrolling) {
                    SDL_Event e{};
                    e.type = SDL_MOUSEWHEEL;
                    e.wheel.preciseX = -dx / 40.f;
                    e.wheel.preciseY = dy / 40.f;
                    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
                    SDL_PushEvent(&e);
                } else {
                    s_x += dx;
                    s_y += dy;
                    push_motion(s_x, s_y, SDL_BUTTON_MMASK);
                }
            }
            if (phase == 2) {
                if (s_panning)
                    push_button(s_x, s_y, false);
                s_scrolling = s_panning = false;
            }
        });
}

extern "C" JNIEXPORT void JNICALL Java_com_prusa3d_slicer_PrusaSlicerActivity_nativeTogglePreferences(JNIEnv*, jclass)
{
    Slic3r::Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread([]() {
        if (Slic3r::App::Android::g_app != nullptr)
            Slic3r::App::Android::g_app->toggle_preferences();
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_prusa3d_slicer_PrusaSlicerActivity_nativeOpenTab(JNIEnv*, jclass, jint place)
{
    Slic3r::Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread([place]() {
        if (Slic3r::App::Android::g_app != nullptr)
            Slic3r::App::Android::g_app->open_tab(Slic3r::App::Android::WebViewPlace(int(place)));
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_prusa3d_slicer_PrusaSlicerActivity_nativeAccountButton(JNIEnv*, jclass)
{
    Slic3r::Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread([]() {
        if (Slic3r::App::Android::g_app != nullptr)
            Slic3r::App::Android::g_app->account_button();
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_prusa3d_slicer_PrusaSlicerActivity_nativeOpenLink(JNIEnv* env, jclass, jstring jurl)
{
    const char* chars = env->GetStringUTFChars(jurl, nullptr);
    std::string url(chars);
    env->ReleaseStringUTFChars(jurl, chars);
    {
        std::lock_guard lock(Slic3r::App::Android::g_early_links_mutex);
        if (!Slic3r::App::Android::g_dispatcher_ready) {
            Slic3r::App::Android::g_early_links.push_back(url);
            return;
        }
    }
    Slic3r::Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread([url]() {
        if (Slic3r::App::Android::g_app != nullptr)
            Slic3r::App::Android::g_app->open_link(url);
    });
}

extern "C" int SDL_main(int argc, char* argv[])
{
    using namespace Slic3r;
    using namespace Slic3r::App;

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        return EXIT_FAILURE;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // OpenGL ES only blits into a single-sampled framebuffer of the same depth format: the ambient
    // occlusion pass copies its DEPTH_COMPONENT24 buffer into the window (Scene.cpp), so no MSAA and
    // no stencil here (unused by the scene) - otherwise everything but the bed stays black on ANGLE.
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

    SDL_Window* window = SDL_CreateWindow("PrusaSlicer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        // No multisampling on this device: retry without it.
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("PrusaSlicer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    if (window == nullptr)
        return EXIT_FAILURE;

    init_common();
    InitParams init_params(argc, argv);
    Android::init_android_paths(init_params);
    init_paths(init_params);

    FileLogConfig file_log_config;
    file_log_config.log_file =(boost::filesystem::path(data_dir()) / "shared_runtime" / "log.txt").string();
    file_log_config.log_file_size = 1 * 1024 * 1024;
    set_file_log_config(file_log_config);
    init_logging();
    // Also to logcat ("adb logcat -s PrusaSlicer"): stdout goes nowhere on Android.
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) {
        l->sinks().push_back(std::make_shared<spdlog::sinks::android_sink_mt>("PrusaSlicer"));
    });
    // PrusaSlicerActivity copies log.txt to Downloads whenever the app goes to the background.
    spdlog::flush_every(std::chrono::seconds(2));
    set_log_level(4);
    init_assert();

    auto& app_services = AppServices::instance();
    app_services.set_app_config(AppConfig::create_app_config());

    Render::TextureManager::set_resource_resolver(std::make_unique<ResourceResolver>(resources_dir(), data_dir()));
    Biz::Scene::BedGeometry::set_resolver([](const std::string& p) -> std::string {
        return Render::TextureManager::resource_resolver().resolve(p);
    });

    int exit_code = EXIT_SUCCESS;
    try {
        Android::AndroidApp app(window);
        app.init();
        app.run();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Fatal error: {}", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PrusaSlicer", e.what(), window);
        flush_logs();
        // Worker threads are still running; tearing down the half-initialised app under them aborts.
        std::_Exit(EXIT_FAILURE);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}

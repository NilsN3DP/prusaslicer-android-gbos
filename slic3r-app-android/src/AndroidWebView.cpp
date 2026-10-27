#include "AndroidWebView.hpp"

#include <SDL.h>
#include <algorithm>
#include <SDL_system.h>
#include <jni.h>

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::App::Android {

namespace {

/// PrusaSlicerActivity.webCommand(int id, String command, String a, String b), see WebPages.java.
void web_command(int id, const char* command, const std::string& a = {}, const std::string& b = {})
{
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetStaticMethodID(cls, "webCommand",
        "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    jstring jcmd = env->NewStringUTF(command);
    jstring ja = env->NewStringUTF(a.c_str());
    jstring jb = env->NewStringUTF(b.c_str());
    env->CallStaticVoidMethod(cls, m, jint(id), jcmd, ja, jb);
    env->DeleteLocalRef(jcmd);
    env->DeleteLocalRef(ja);
    env->DeleteLocalRef(jb);
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

std::string join(const std::vector<std::string>& v)
{
    std::string out;
    for (const std::string& s : v)
        out += (out.empty() ? "" : ",") + s;
    return out;
}

bool is_web_url(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || url.rfind("file://", 0) == 0;
}

} // namespace

AndroidWebView::AndroidWebView(
    int id,
    WebViewPlace place,
    std::unique_ptr<Browser::AbstractBrowserLogic> logic,
    Biz::ProjectInteractor* project_interactor,
    CloseCallback on_close
) :
    m_id(id),
    m_place(place),
    m_logic(std::move(logic)),
    m_project_interactor(project_interactor),
    m_on_close(std::move(on_close))
{
    if (m_project_interactor != nullptr)
        m_project_interactor->user_account_interactor().add_listener<Biz::UserAccount::IUserAccountListener>(this);
}

AndroidWebView::~AndroidWebView()
{
    if (m_project_interactor != nullptr)
        m_project_interactor->user_account_interactor().remove_listener<Biz::UserAccount::IUserAccountListener>(this);
}

// Chrome on the Googlebook does not hand the prusaslicer://login redirect back to the app: show
// the Prusa Account login in the dialog's own WebView, which catches the redirect itself
// (Google / Apple / Facebook refuse embedded web views and stay in the browser).
bool AndroidWebView::run(std::vector<Browser::BrowserLogicCommand>&& commands)
{
    auto is_inline_login = [this](const Browser::BrowserLogicCommand& command) {
        return command.type == Browser::BrowserLogicCommandType::OpenExternalBrowserForced && m_place == WebViewPlace::Dialog
            && command.data.find("account.prusa3d.com") != std::string::npos;
    };
    // The "continue in your browser" page would only end up in the back history.
    const bool inline_login = std::any_of(commands.begin(), commands.end(), is_inline_login);
    bool result = true;
    for (const Browser::BrowserLogicCommand& command : commands) {
        if (is_inline_login(command)) {
            handle_logic_command_LoadURL(command.data);
            continue;
        }
        if (inline_login && command.type == Browser::BrowserLogicCommandType::LoadResourcesPage)
            continue;
        result = handle_logic_command(command) && result;
    }
    return result;
}

void AndroidWebView::start()
{
    web_command(m_id, "create", std::to_string(int(m_place)), join(m_logic->script_message_handler_names()));
    if (m_place == WebViewPlace::Dialog) {
        // Like WebViewDialog: the logic's url, else its loading page, whose "loaded" event
        // starts the logic (e.g. the Prusa Account login redirect).
        if (!m_logic->url().empty())
            handle_logic_command_LoadURL(m_logic->url());
        else
            handle_logic_command_LoadResourcesPage(m_logic->loading_html());
        run(m_logic->on_webview_created());
    } else {
        run(m_logic->on_webview_created());
        run(m_logic->on_load_default_url());
    }
    run(m_logic->on_show_webview_event(true));
}

void AndroidWebView::handle_event(WebViewEvent event, const std::string& a, const std::string& b)
{
    if (m_ended)
        return;
    switch (event) {
    case WebViewEvent::Navigation: {
        // Java holds every user navigation; it proceeds only if the logic does not veto it.
        const bool allowed = run(m_logic->on_navigation_request_webview_event(a, b));
        if (allowed && is_web_url(a))
            web_command(m_id, "allow", a);
        break;
    }
    case WebViewEvent::PageStarted:
        run(m_logic->on_page_will_load_webview_event());
        break;
    case WebViewEvent::PageFinished:
        m_current_url = a;
        run(m_logic->on_loaded_webview_event(a));
        break;
    case WebViewEvent::ScriptMessage:
        run(m_logic->on_script_message_webview_event(a));
        break;
    case WebViewEvent::Shown:
        run(m_logic->on_show_webview_event(a == "1"));
        break;
    case WebViewEvent::Closed:
        end(false);
        break;
    case WebViewEvent::LoadError:
        if (m_load_default_url_on_error)
            run(m_logic->on_load_default_url());
        else
            handle_logic_command_LoadResourcesPage(m_logic->error_html());
        break;
    }
}

void AndroidWebView::end(bool ok)
{
    if (m_ended)
        return;
    m_ended = true;
    web_command(m_id, "close");
    if (m_on_close)
        m_on_close(ok, *m_logic);
    // Destroyed later: we may be inside one of our own handlers right now.
    const int id = m_id;
    Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread_after(
        [id]() { WebViewManager::instance().destroy(id); });
}

// --- IUserAccountListener --------------------------------------------------------------------

void AndroidWebView::on_user_account_id_success(bool is_refresh, const std::string&)
{
    if (!m_ended)
        run(m_logic->on_user_account_id_success(is_refresh, m_current_url));
}

void AndroidWebView::on_user_account_logged_out()
{
    if (!m_ended)
        run(m_logic->on_user_account_logged_out(m_current_url));
}

void AndroidWebView::on_user_account_will_refresh()
{
    if (!m_ended)
        run(m_logic->on_user_account_will_refresh());
}

void AndroidWebView::on_printables_secret_token(const std::string& body)
{
    if (!m_ended)
        run(m_logic->on_printables_secret_token(body));
}

// --- logic commands ----------------------------------------------------------------------------

bool AndroidWebView::handle_logic_command_LoadURL(const std::string& data)
{
    web_command(m_id, "load", data);
    return true;
}

bool AndroidWebView::handle_logic_command_LoadRequest(const std::string& data)
{
    // Same as the desktop: the access token travels in the Authorization header.
    const std::string token = m_logic->access_token();
    web_command(m_id, "loadRequest", data, token.empty() ? std::string() : "External " + token);
    return true;
}

bool AndroidWebView::handle_logic_command_RunScript(const std::string& data)
{
    web_command(m_id, "runScript", data);
    return true;
}

bool AndroidWebView::handle_logic_command_EndModalOK(const std::string&)
{
    end(true);
    return true;
}

bool AndroidWebView::handle_logic_command_EndModalCancel(const std::string&)
{
    end(false);
    return true;
}

bool AndroidWebView::handle_logic_command_DeleteCookies(const std::string& data)
{
    web_command(m_id, "deleteCookies", data);
    return true;
}

bool AndroidWebView::handle_logic_command_DeleteCookiesWithCounter(const std::string& data)
{
    web_command(m_id, "deleteCookies", data);
    return true;
}

bool AndroidWebView::handle_logic_command_Veto(const std::string&)
{
    return false; // reported to the caller as "not allowed"
}

bool AndroidWebView::handle_logic_command_DoReload(const std::string&)
{
    web_command(m_id, "reload");
    return true;
}

bool AndroidWebView::handle_logic_command_AddUserScript(const std::string& data)
{
    web_command(m_id, "addUserScript", data);
    return true;
}

bool AndroidWebView::handle_logic_command_LoadResourcesPage(const std::string& data)
{
    namespace fs = boost::filesystem;
    const bool dark = AppServices::instance().app_config().get<Theme::Style>("theme") == Theme::Style::Dark;
    const fs::path web = fs::path(resources_dir()) / "web";
    fs::path page = web / (data + (dark ? "_dark" : "") + ".html");
    if (!fs::exists(page))
        page = web / (data + ".html");
    web_command(m_id, "load", "file://" + page.generic_string());
    return true;
}

bool AndroidWebView::handle_logic_command_RegisterPrusaSlicerURL(const std::string&)
{
    return true; // the prusaslicer:// scheme is registered in AndroidManifest.xml
}

bool AndroidWebView::handle_logic_command_SetLoadDefaultURLOnErrorTrue(const std::string&)
{
    m_load_default_url_on_error = true;
    return true;
}

bool AndroidWebView::handle_logic_command_SetLoadDefaultURLOnErrorFalse(const std::string&)
{
    m_load_default_url_on_error = false;
    return true;
}

bool AndroidWebView::handle_logic_command_SwitchToSlicing(const std::string&)
{
    web_command(m_id, "switchToSlicing");
    return true;
}

bool AndroidWebView::handle_logic_command_SetBasicAuth(const std::string& data)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(data);
        web_command(m_id, "basicAuth", j["username"].get<std::string>(), j["password"].get<std::string>());
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse basic auth payload: {}", e.what());
        return false;
    }
    return true;
}

bool AndroidWebView::handle_logic_command_ClearBasicAuth(const std::string&)
{
    web_command(m_id, "basicAuth");
    return true;
}

// --- manager -----------------------------------------------------------------------------------

WebViewManager& WebViewManager::instance()
{
    static WebViewManager manager;
    return manager;
}

int WebViewManager::open(
    WebViewPlace place,
    std::unique_ptr<Browser::AbstractBrowserLogic> logic,
    Biz::ProjectInteractor* project_interactor,
    AndroidWebView::CloseCallback on_close
)
{
    const int id = m_next_id++;
    auto view = std::make_unique<AndroidWebView>(id, place, std::move(logic), project_interactor, std::move(on_close));
    AndroidWebView& ref = *view;
    m_views.emplace(id, std::move(view));
    ref.start();
    return id;
}

void WebViewManager::handle_event(int id, WebViewEvent event, const std::string& a, const std::string& b)
{
    if (auto it = m_views.find(id); it != m_views.end())
        it->second->handle_event(event, a, b);
}

void WebViewManager::destroy(int id)
{
    m_views.erase(id);
}

bool WebViewManager::has_tab(WebViewPlace place) const
{
    for (const auto& [id, view] : m_views)
        if (view->place() == place)
            return true;
    return false;
}

} // namespace Slic3r::App::Android

// Java (UI thread) -> SDL thread
extern "C" JNIEXPORT void JNICALL Java_com_prusa3d_slicer_PrusaSlicerActivity_nativeWebViewEvent(
    JNIEnv* env, jclass, jint id, jint event, jstring ja, jstring jb)
{
    auto to_string = [env](jstring s) {
        if (s == nullptr)
            return std::string();
        const char* chars = env->GetStringUTFChars(s, nullptr);
        std::string r(chars);
        env->ReleaseStringUTFChars(s, chars);
        return r;
    };
    std::string a = to_string(ja);
    std::string b = to_string(jb);
    Slic3r::Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread(
        [id = int(id), event = int(event), a = std::move(a), b = std::move(b)]() {
            Slic3r::App::Android::WebViewManager::instance().handle_event(
                id, Slic3r::App::Android::WebViewEvent(event), a, b);
        });
}

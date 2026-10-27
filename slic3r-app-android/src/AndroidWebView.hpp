#pragma once

// Android counterpart of slic3r-shared-wx's WebViewPanel / WebViewDialog: hosts one of PrusaSlicer's
// Browser::AbstractBrowserLogic (Connect, Printables, Prusa Account login, ...) in an Android WebView
// (PrusaSlicerActivity / WebPages.java). The logic lives on the SDL thread; the WebView on the UI
// thread. Events travel Java -> native through nativeWebViewEvent(), commands native -> Java via JNI.

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include "Slic3r/App/Browser/AbstractBrowserLogicCommandHandler.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::Android {

/// Where the web view is shown (matches WebPages.java).
enum class WebViewPlace : int
{
    Dialog     = 0, ///< full window overlay with a close button (login, Connect printer selection)
    Connect    = 1, ///< side bar "Connect" tab
    Printables = 2, ///< side bar "Printables" tab
};

/// Events sent by WebPages.java (nativeWebViewEvent).
enum class WebViewEvent : int
{
    Navigation    = 0, ///< a: new url, b: current url (the navigation is held until the logic allows it)
    PageStarted   = 1,
    PageFinished  = 2, ///< a: url
    ScriptMessage = 3, ///< a: message posted to one of the logic's script message handlers
    Shown         = 4, ///< a: "1" or "0"
    Closed        = 5, ///< the user closed a dialog
    LoadError     = 6,
};

class AndroidWebView :
    public Browser::AbstractBrowserLogicCommandHandler,
    public Biz::UserAccount::IUserAccountListener
{
public:
    using CloseCallback = std::function<void(bool ok, Browser::AbstractBrowserLogic& logic)>;

    AndroidWebView(
        int id,
        WebViewPlace place,
        std::unique_ptr<Browser::AbstractBrowserLogic> logic,
        Biz::ProjectInteractor* project_interactor,
        CloseCallback on_close
    );
    ~AndroidWebView() override;

    /// Creates the Java WebView and loads the logic's default page.
    void start();
    void handle_event(WebViewEvent event, const std::string& a, const std::string& b);

    int id() const { return m_id; }
    WebViewPlace place() const { return m_place; }

    // IUserAccountListener
    void on_user_account_id_success(bool is_refresh, const std::string& username) override;
    void on_user_account_logged_out() override;
    void on_user_account_will_refresh() override;
    void on_printables_secret_token(const std::string& body) override;

protected:
    bool handle_logic_command_LoadURL(const std::string& data) override;
    bool handle_logic_command_LoadRequest(const std::string& data) override;
    bool handle_logic_command_RunScript(const std::string& data) override;
    bool handle_logic_command_EndModalOK(const std::string& data) override;
    bool handle_logic_command_EndModalCancel(const std::string& data) override;
    bool handle_logic_command_DeleteCookies(const std::string& data) override;
    bool handle_logic_command_DeleteCookiesWithCounter(const std::string& data) override;
    bool handle_logic_command_Veto(const std::string& data) override;
    bool handle_logic_command_DoReload(const std::string& data) override;
    bool handle_logic_command_AddUserScript(const std::string& data) override;
    bool handle_logic_command_LoadResourcesPage(const std::string& data) override;
    bool handle_logic_command_RegisterPrusaSlicerURL(const std::string& data) override;
    bool handle_logic_command_SetLoadDefaultURLOnErrorTrue(const std::string& data) override;
    bool handle_logic_command_SetLoadDefaultURLOnErrorFalse(const std::string& data) override;
    bool handle_logic_command_SwitchToSlicing(const std::string& data) override;
    bool handle_logic_command_SetBasicAuth(const std::string& data) override;
    bool handle_logic_command_ClearBasicAuth(const std::string& data) override;

private:
    bool run(std::vector<Browser::BrowserLogicCommand>&& commands);
    void end(bool ok);

    int m_id;
    WebViewPlace m_place;
    std::unique_ptr<Browser::AbstractBrowserLogic> m_logic;
    Biz::ProjectInteractor* m_project_interactor;
    CloseCallback m_on_close;
    std::string m_current_url;
    bool m_load_default_url_on_error{false};
    bool m_ended{false};
};

/// Owns the hosted web views; all calls on the SDL thread.
class WebViewManager
{
public:
    static WebViewManager& instance();

    /// Shows `logic` (replacing a tab of the same place). Returns the web view id.
    int open(
        WebViewPlace place,
        std::unique_ptr<Browser::AbstractBrowserLogic> logic,
        Biz::ProjectInteractor* project_interactor,
        AndroidWebView::CloseCallback on_close = {}
    );
    void handle_event(int id, WebViewEvent event, const std::string& a, const std::string& b);
    /// Removes a view; the Java side has already dropped it or is told to.
    void destroy(int id);
    bool has_tab(WebViewPlace place) const;

private:
    std::map<int, std::unique_ptr<AndroidWebView>> m_views;
    int m_next_id{1};
};

} // namespace Slic3r::App::Android

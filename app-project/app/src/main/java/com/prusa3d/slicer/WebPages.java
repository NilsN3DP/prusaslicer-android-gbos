package com.prusa3d.slicer;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.CookieManager;
import android.webkit.HttpAuthHandler;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.RelativeLayout;

import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Android WebViews for PrusaSlicer's browser logics (AndroidWebView.cpp): the Connect / Printables
 * side bar tabs and web dialogs (Prusa Account login, choosing a Connect printer).
 * All methods run on the UI thread.
 */
final class WebPages {
    // WebViewPlace in AndroidWebView.hpp
    static final int PLACE_DIALOG = 0, PLACE_CONNECT = 1, PLACE_PRINTABLES = 2;
    // WebViewEvent in AndroidWebView.hpp
    private static final int EV_NAVIGATION = 0, EV_PAGE_STARTED = 1, EV_PAGE_FINISHED = 2,
            EV_SCRIPT_MESSAGE = 3, EV_SHOWN = 4, EV_CLOSED = 5, EV_LOAD_ERROR = 6;

    private static final Set<String> ALL_ORIGINS = Collections.singleton("*");

    private final Activity mActivity;
    private final RelativeLayout mContent;
    private final int mBarId;
    private final Map<Integer, Page> mPages = new HashMap<>();
    private int mVisibleTab = -1;

    private static final class Page {
        int id;
        int place;
        WebView view;
        View container;          // the dialog frame, or the view itself for tabs
        String allowedUrl;       // navigation the native logic already approved
        String authUser, authPassword;
        final List<String> pendingScripts = new ArrayList<>(); // without document-start support
    }

    WebPages(Activity activity, RelativeLayout content, int sideBarId) {
        mActivity = activity;
        mContent = content;
        mBarId = sideBarId;
    }

    /** PrusaSlicerActivity.webCommand(): commands from the native browser logic. */
    void command(int id, String cmd, String a, String b) {
        if ("create".equals(cmd)) {
            create(id, Integer.parseInt(a), b);
            return;
        }
        if ("switchToSlicing".equals(cmd)) {
            PrusaSlicerActivity.showSlicing();
            return;
        }
        if ("deleteCookies".equals(cmd)) {
            deleteCookies(a);
            return;
        }
        Page page = mPages.get(id);
        if (page == null)
            return;
        switch (cmd) {
            case "load":
            case "allow":
                page.allowedUrl = a;
                page.view.loadUrl(a);
                break;
            case "loadRequest": {
                page.allowedUrl = a;
                Map<String, String> headers = new HashMap<>();
                if (!b.isEmpty())
                    headers.put("Authorization", b);
                page.view.loadUrl(a, headers);
                break;
            }
            case "runScript":
                page.view.evaluateJavascript(a, null);
                break;
            case "addUserScript":
                addDocumentStartScript(page, a);
                break;
            case "reload":
                page.view.reload();
                break;
            case "basicAuth":
                page.authUser = a.isEmpty() ? null : a;
                page.authPassword = b;
                break;
            case "close":
                remove(page);
                break;
        }
    }

    /** Shows the tab of `place` (Connect / Printables) and hides the others. */
    void showTab(int place) {
        mVisibleTab = place;
        for (Page page : mPages.values()) {
            if (page.place == PLACE_DIALOG)
                continue;
            boolean visible = page.place == place;
            if ((page.container.getVisibility() == View.VISIBLE) != visible) {
                page.container.setVisibility(visible ? View.VISIBLE : View.GONE);
                PrusaSlicerActivity.nativeWebViewEvent(page.id, EV_SHOWN, visible ? "1" : "0", "");
            }
        }
    }

    boolean hasDialog() {
        return hasTab(PLACE_DIALOG);
    }

    boolean hasTab(int place) {
        for (Page page : mPages.values())
            if (page.place == place)
                return true;
        return false;
    }

    /** Back key: a dialog closes, a tab goes back in its history. */
    boolean onBackPressed(int visibleTabPlace) {
        for (Page page : mPages.values()) {
            if (page.place == PLACE_DIALOG) {
                if (canGoBackToWeb(page.view))
                    page.view.goBack();
                else
                    closeByUser(page);
                return true;
            }
        }
        for (Page page : mPages.values()) {
            if (page.place == visibleTabPlace && canGoBackToWeb(page.view)) {
                page.view.goBack();
                return true;
            }
        }
        return false;
    }

    /** PrusaSlicer's local loading pages (file://) are no place to go back to. */
    private static boolean canGoBackToWeb(WebView view) {
        android.webkit.WebBackForwardList history = view.copyBackForwardList();
        int index = history.getCurrentIndex() - 1;
        return index >= 0 && !history.getItemAtIndex(index).getUrl().startsWith("file:");
    }

    @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
    private void create(int id, int place, String handlerNames) {
        Page page = new Page();
        page.id = id;
        page.place = place;
        WebView view = new WebView(mActivity);
        page.view = view;

        WebSettings s = view.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        s.setAllowFileAccess(true); // PrusaSlicer's own loading / error pages in resources/web
        // Like WX::WebView: Printables and Connect show their slicer features only to this user agent.
        s.setUserAgentString("PrusaSlicer/" + appVersion() + " (Linux)");
        CookieManager.getInstance().setAcceptThirdPartyCookies(view, true);
        view.setBackgroundColor(Color.rgb(0x1e, 0x1e, 0x1e));

        // The pages talk to PrusaSlicer through window.<handler>.postMessage(...) (and the WebKit
        // spelling window.webkit.messageHandlers.<handler>.postMessage). Messages may be objects.
        StringBuilder shim = new StringBuilder("(function(){window.webkit=window.webkit||{};"
                + "window.webkit.messageHandlers=window.webkit.messageHandlers||{};");
        for (String name : handlerNames.split(",")) {
            if (name.isEmpty())
                continue;
            String nativeName = "__prusaNative_" + name;
            view.addJavascriptInterface(new Bridge(id), nativeName);
            shim.append("(function(n,b){var h={postMessage:function(m){b.postMessage(typeof m==='string'?m:JSON.stringify(m));}};")
                .append("window[n]=h;window.webkit.messageHandlers[n]=h;})('")
                .append(name).append("',window['").append(nativeName).append("']);");
        }
        shim.append("})();");
        addDocumentStartScript(page, shim.toString());

        view.setWebViewClient(new Client(page));
        view.setDownloadListener((url, userAgent, contentDisposition, mimetype, length) ->
                mActivity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))));

        if (place == PLACE_DIALOG) {
            FrameLayout frame = new FrameLayout(mActivity);
            frame.setBackgroundColor(Color.argb(0xc0, 0, 0, 0));
            frame.setClickable(true);
            FrameLayout.LayoutParams vp = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
            int margin = dp(48);
            vp.setMargins(margin, margin, margin, margin);
            frame.addView(view, vp);

            ImageButton close = new ImageButton(mActivity);
            close.setImageResource(android.R.drawable.ic_menu_close_clear_cancel);
            close.setBackgroundColor(Color.TRANSPARENT);
            close.setOnClickListener(v -> closeByUser(page));
            FrameLayout.LayoutParams cp = new FrameLayout.LayoutParams(dp(40), dp(40), Gravity.TOP | Gravity.END);
            cp.setMargins(0, dp(4), dp(4), 0);
            frame.addView(close, cp);

            page.container = frame;
            mContent.addView(frame, new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        } else {
            page.container = view;
            if (place != mVisibleTab)
                view.setVisibility(View.GONE);
            RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
            params.addRule(RelativeLayout.END_OF, mBarId);
            mContent.addView(view, params);
        }
        mPages.put(id, page);
    }

    private void addDocumentStartScript(Page page, String script) {
        if (WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT))
            WebViewCompat.addDocumentStartJavaScript(page.view, script, ALL_ORIGINS);
        else
            page.pendingScripts.add(script); // run in onPageStarted instead
    }

    private void closeByUser(Page page) {
        remove(page);
        PrusaSlicerActivity.nativeWebViewEvent(page.id, EV_CLOSED, "", "");
    }

    private void remove(Page page) {
        if (mPages.remove(page.id) == null)
            return;
        mContent.removeView(page.container);
        page.view.destroy();
    }

    private void deleteCookies(String url) {
        CookieManager cookies = CookieManager.getInstance();
        String all = cookies.getCookie(url);
        if (all == null)
            return;
        for (String cookie : all.split(";")) {
            String name = cookie.split("=", 2)[0].trim();
            cookies.setCookie(url, name + "=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
        }
        cookies.flush();
    }

    private final class Bridge {
        private final int mId;

        Bridge(int id) { mId = id; }

        @JavascriptInterface
        public void postMessage(String message) {
            PrusaSlicerActivity.nativeWebViewEvent(mId, EV_SCRIPT_MESSAGE, message == null ? "" : message, "");
        }
    }

    private final class Client extends WebViewClient {
        private final Page mPage;

        Client(Page page) { mPage = page; }

        @Override
        public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest request) {
            if (!request.isForMainFrame())
                return false;
            String url = request.getUrl().toString();
            if (url.equals(mPage.allowedUrl)) {
                mPage.allowedUrl = null;
                return false;
            }
            android.util.Log.i("PrusaSlicer", "web navigation " + url.replaceAll("(code|token)=[^&]*", "$1=…") + " redirect=" + request.isRedirect());
            // Held until the native logic answers: it loads the url ("allow") unless it vetoes.
            String current = v.getUrl();
            PrusaSlicerActivity.nativeWebViewEvent(mPage.id, EV_NAVIGATION, url, current == null ? "" : current);
            return true;
        }

        @Override
        public void onPageStarted(WebView v, String url, android.graphics.Bitmap favicon) {
            for (String script : mPage.pendingScripts)
                v.evaluateJavascript(script, null);
            PrusaSlicerActivity.nativeWebViewEvent(mPage.id, EV_PAGE_STARTED, url, "");
        }

        @Override
        public void onPageFinished(WebView v, String url) {
            PrusaSlicerActivity.nativeWebViewEvent(mPage.id, EV_PAGE_FINISHED, url, "");
        }

        @Override
        public void onReceivedError(WebView v, WebResourceRequest request, WebResourceError error) {
            if (request.isForMainFrame())
                PrusaSlicerActivity.nativeWebViewEvent(mPage.id, EV_LOAD_ERROR,
                        request.getUrl().toString(), String.valueOf(error.getDescription()));
        }

        @Override
        public void onReceivedHttpAuthRequest(WebView v, HttpAuthHandler handler, String host, String realm) {
            if (mPage.authUser != null)
                handler.proceed(mPage.authUser, mPage.authPassword);
            else
                super.onReceivedHttpAuthRequest(v, handler, host, realm);
        }
    }

    private String appVersion() {
        try {
            return mActivity.getPackageManager().getPackageInfo(mActivity.getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "3.0.0";
        }
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                mActivity.getResources().getDisplayMetrics());
    }
}

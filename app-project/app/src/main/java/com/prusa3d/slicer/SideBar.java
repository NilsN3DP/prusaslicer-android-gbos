package com.prusa3d.slicer;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.PorterDuff;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;

/**
 * The desktop app's left bar (slic3r-app-desktop/LeftBar*), rebuilt with Android views:
 * Slicing / Connect / Printables tabs on top, preferences and account at the bottom.
 * Connect and Printables run PrusaSlicer's own page logics (WebPages / AndroidWebView.cpp), so
 * choosing a printer, sending G-code and "Open in PrusaSlicer" work like on the desktop.
 */
final class SideBar {
    private static final int BAR_WIDTH_DP = 72;
    private static final int COLOR_BAR = Color.rgb(0x26, 0x26, 0x26);
    private static final int COLOR_SELECTED = Color.rgb(0x3b, 0x5a, 0x94);
    private static final int COLOR_ICON = Color.rgb(0xd0, 0xd0, 0xd0);
    private static final int COLOR_LABEL = Color.rgb(0xb8, 0xb8, 0xb8);

    // tab ids are the WebPages places; Slicing is PrusaSlicer's own surface
    private static final int TAB_SLICING = -1;

    private final Activity mActivity;
    private final RelativeLayout mContent;
    private final View mSurface;
    private final LinearLayout mBar;
    private final WebPages mWebPages;
    private View mSlicingItem, mConnectItem, mPrintablesItem, mPreferencesItem;
    private int mTab = TAB_SLICING;

    /** Called from native code when PrusaSlicer opens / closes its preferences dialog. */
    private boolean mPreferencesSelected;

    SideBar(Activity activity, ViewGroup sdlLayout, View sdlSurface) {
        mActivity = activity;
        mSurface = sdlSurface;
        mContent = (RelativeLayout) sdlLayout;
        int barWidth = dp(BAR_WIDTH_DP);

        mBar = new LinearLayout(activity);
        mBar.setOrientation(LinearLayout.VERTICAL);
        mBar.setBackgroundColor(COLOR_BAR);
        mBar.setId(View.generateViewId());
        mBar.setPadding(0, dp(6), 0, dp(6));
        mWebPages = new WebPages(activity, mContent, mBar.getId());

        mSlicingItem = addItem(R.drawable.ic_lb_slicing, "Slicing", v -> selectTab(TAB_SLICING));
        mConnectItem = addItem(R.drawable.ic_lb_connect, "Connect", v -> selectTab(WebPages.PLACE_CONNECT));
        mPrintablesItem = addItem(R.drawable.ic_lb_printables, "Printables", v -> selectTab(WebPages.PLACE_PRINTABLES));

        View spacer = new View(activity);
        mBar.addView(spacer, new LinearLayout.LayoutParams(1, 0, 1f));

        mPreferencesItem = addItem(R.drawable.ic_lb_preferences, null, v -> {
            // Like the desktop: the cog always works on the Slicing page.
            if (mTab != TAB_SLICING)
                selectTab(TAB_SLICING);
            PrusaSlicerActivity.nativeTogglePreferences();
        });
        addItem(R.drawable.ic_lb_account, null, v -> {
            if (mTab != TAB_SLICING)
                selectTab(TAB_SLICING);
            PrusaSlicerActivity.nativeAccountButton();
        });

        RelativeLayout.LayoutParams barParams =
                new RelativeLayout.LayoutParams(barWidth, ViewGroup.LayoutParams.MATCH_PARENT);
        barParams.addRule(RelativeLayout.ALIGN_PARENT_START);
        mContent.addView(mBar, barParams);

        // PrusaSlicer's surface starts right of the bar (it gets its size from the surface).
        RelativeLayout.LayoutParams surfaceParams = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        surfaceParams.addRule(RelativeLayout.END_OF, mBar.getId());
        mSurface.setLayoutParams(surfaceParams);

        updateSelection();
    }

    WebPages webPages() {
        return mWebPages;
    }

    void setPreferencesSelected(boolean selected) {
        mPreferencesSelected = selected;
        updateSelection();
    }

    void showSlicing() {
        mActivity.runOnUiThread(() -> selectTab(TAB_SLICING));
    }

    boolean isWebPageShown() {
        return mTab != TAB_SLICING || mWebPages.hasDialog();
    }

    /** Back button: close a web dialog, leave a web page to its previous page, or back to Slicing. */
    boolean onBackPressed() {
        if (mWebPages.onBackPressed(mTab))
            return true;
        if (mTab == TAB_SLICING)
            return false;
        selectTab(TAB_SLICING);
        return true;
    }

    private void selectTab(int tab) {
        mTab = tab;
        if (tab != TAB_SLICING && !mWebPages.hasTab(tab))
            PrusaSlicerActivity.nativeOpenTab(tab); // the page appears once native code created it
        mWebPages.showTab(tab);
        if (tab == TAB_SLICING)
            mSurface.requestFocus();
        updateSelection();
    }

    private View addItem(int icon, String label, View.OnClickListener onClick) {
        LinearLayout item = new LinearLayout(mActivity);
        item.setOrientation(LinearLayout.VERTICAL);
        item.setGravity(Gravity.CENTER);
        item.setPadding(0, dp(8), 0, dp(8));
        item.setClickable(true);
        item.setFocusable(false);
        item.setOnClickListener(onClick);

        ImageView image = new ImageView(mActivity);
        image.setImageResource(icon);
        image.setColorFilter(COLOR_ICON, PorterDuff.Mode.SRC_IN);
        item.addView(image, new LinearLayout.LayoutParams(dp(24), dp(24)));

        if (label != null) {
            TextView text = new TextView(mActivity);
            text.setText(label);
            text.setTextColor(COLOR_LABEL);
            text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
            text.setGravity(Gravity.CENTER);
            text.setPadding(0, dp(3), 0, 0);
            item.addView(text, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        }

        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(60), ViewGroup.LayoutParams.WRAP_CONTENT);
        params.gravity = Gravity.CENTER_HORIZONTAL;
        params.setMargins(0, dp(2), 0, dp(2));
        mBar.addView(item, params);
        return item;
    }

    private void updateSelection() {
        setSelected(mSlicingItem, mTab == TAB_SLICING && !mPreferencesSelected);
        setSelected(mConnectItem, mTab == WebPages.PLACE_CONNECT);
        setSelected(mPrintablesItem, mTab == WebPages.PLACE_PRINTABLES);
        setSelected(mPreferencesItem, mPreferencesSelected);
    }

    private void setSelected(View item, boolean selected) {
        if (item == null)
            return;
        if (selected) {
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(COLOR_SELECTED);
            bg.setCornerRadius(dp(6));
            item.setBackground(bg);
        } else {
            item.setBackground(null);
        }
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                mActivity.getResources().getDisplayMetrics());
    }
}

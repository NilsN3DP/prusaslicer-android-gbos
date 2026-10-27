package com.prusa3d.slicer;

import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.text.InputType;
import android.util.Log;
import android.view.MotionEvent;
import android.widget.EditText;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.concurrent.CountDownLatch;

/**
 * Hosts PrusaSlicer's native code (libmain.so, see android-native/slic3r-app-android).
 *
 * The static methods are called from the SDL thread through JNI (AndroidBridge.cpp). They show
 * their UI on the UI thread and block the SDL thread until the user has answered.
 */
public class PrusaSlicerActivity extends SDLActivity {
    private static final String TAG = "PrusaSlicer";
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_SAVE = 1002;

    private static PrusaSlicerActivity sInstance;

    // Result hand-over between onActivityResult (UI thread) and the waiting SDL thread.
    private static CountDownLatch sPickerLatch;
    private static Intent sPickerResult;

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        sInstance = this;
        // Must be ready before SDL_main runs, which reads PrusaSlicer's resources from disk.
        unpackResources();
        super.onCreate(savedInstanceState);
        if (mLayout != null && mSurface != null) {
            sSideBar = new SideBar(this, mLayout, mSurface);
            // Android 15+ draws apps edge-to-edge: keep everything clear of status / navigation bars.
            mLayout.setOnApplyWindowInsetsListener((v, insets) -> {
                android.graphics.Insets bars = insets.getInsets(
                        android.view.WindowInsets.Type.systemBars() | android.view.WindowInsets.Type.displayCutout());
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
                return insets;
            });
            mSurface.setOnDragListener(this::onDrag);
        }
        requestAllFilesAccess();
        handleLinkIntent(getIntent());
    }

    // On the Googlebook desktop the app stays visible next to the Files app (no onStop):
    // export the log every few seconds instead.
    private final android.os.Handler mLogHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable mLogExport = new Runnable() {
        @Override
        public void run() {
            new Thread(PrusaSlicerActivity.this::exportLog).start();
            mLogHandler.postDelayed(this, 5000);
        }
    };
    private static final java.util.ArrayDeque<String> sJavaLog = new java.util.ArrayDeque<>();

    /** Java side diagnostics, appended to the exported log. */
    static void javaLog(String line) {
        synchronized (sJavaLog) {
            sJavaLog.addLast(new java.util.Date() + " " + line);
            while (sJavaLog.size() > 300)
                sJavaLog.removeFirst();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        mLogHandler.removeCallbacks(mLogExport);
        mLogHandler.postDelayed(mLogExport, 3000);
    }

    @Override
    protected void onStop() {
        super.onStop();
        mLogHandler.removeCallbacks(mLogExport);
        exportLog();
    }

    /**
     * Copies PrusaSlicer's log (data dir/shared_runtime/log.txt) to Downloads/PrusaSlicer-log.txt,
     * so it can be shared without adb. MediaStore needs no storage permission for that.
     */
    private Uri mLogUri;
    // Preferences -> "Log in Downloads speichern" (AndroidApp.cpp); on until PrusaSlicer says otherwise.
    private static volatile boolean sLogExport = true;

    /** Called from native code when the preference is read or changed. */
    public static void setLogExport(boolean enabled) {
        if (sLogExport == enabled)
            return;
        sLogExport = enabled;
        if (!enabled && sInstance != null)
            new Thread(sInstance::deleteExportedLog).start();
    }

    /** Removes this app's log copies from Downloads (also those of earlier sessions). */
    private synchronized void deleteExportedLog() {
        Uri collection = android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI;
        try (Cursor c = getContentResolver().query(collection, new String[] { android.provider.MediaStore.MediaColumns._ID },
                android.provider.MediaStore.MediaColumns.DISPLAY_NAME + " LIKE ?", new String[] { "PrusaSlicer-log%" }, null)) {
            while (c != null && c.moveToNext()) {
                try {
                    getContentResolver().delete(android.content.ContentUris.withAppendedId(collection, c.getLong(0)), null, null);
                } catch (Exception ignored) {
                    // not ours (e.g. from an earlier installation)
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Deleting the exported log failed", e);
        }
        mLogUri = null;
    }

    private synchronized void exportLog() {
        if (!sLogExport)
            return;
        File log = new File(getFilesDir(), "PrusaSlicer/shared_runtime/log.txt");
        if (!log.isFile())
            return;
        try {
            android.content.ContentResolver resolver = getContentResolver();
            Uri collection = android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI;
            String name = "PrusaSlicer-log.txt";
            Uri target = mLogUri;
            if (target == null) {
                // One copy only: drop this app's earlier exports ("PrusaSlicer-log (3).txt" ...).
                try (Cursor c = resolver.query(collection, new String[] { android.provider.MediaStore.MediaColumns._ID },
                        android.provider.MediaStore.MediaColumns.DISPLAY_NAME + " LIKE ?", new String[] { "PrusaSlicer-log%" }, null)) {
                    while (c != null && c.moveToNext()) {
                        try {
                            resolver.delete(android.content.ContentUris.withAppendedId(collection, c.getLong(0)), null, null);
                        } catch (Exception ignored) {
                            // not ours (e.g. from an earlier installation)
                        }
                    }
                }
            }
            if (target == null) {
                android.content.ContentValues values = new android.content.ContentValues();
                values.put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, name);
                values.put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                target = resolver.insert(collection, values);
            }
            if (target == null)
                return;
            mLogUri = target;
            try (InputStream in = new java.io.FileInputStream(log); OutputStream out = resolver.openOutputStream(target, "wt")) {
                copy(in, out);
                StringBuilder java = new StringBuilder("\n===== Java =====\n");
                synchronized (sJavaLog) {
                    for (String line : sJavaLog)
                        java.append(line).append('\n');
                }
                out.write(java.toString().getBytes());
            }
        } catch (Exception e) {
            Log.w(TAG, "Exporting the log failed", e);
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleLinkIntent(intent);
    }

    // ---------------------------------------------------------------------------------------
    // prusaslicer:// links (Printables "Open in PrusaSlicer", Prusa Account login redirect)
    // ---------------------------------------------------------------------------------------

    /** Hands a prusaslicer:// link to PrusaSlicer (AndroidApp.cpp, queued until it is ready). */
    static native void nativeOpenLink(String url);

    private void handleLinkIntent(Intent intent) {
        if (intent == null)
            return;
        String action = intent.getAction();
        ArrayList<Uri> files = new ArrayList<>();
        if (Intent.ACTION_VIEW.equals(action) && intent.getData() != null) {
            String scheme = intent.getData().getScheme();
            if ("content".equals(scheme) || "file".equals(scheme))
                files.add(intent.getData());
            else
                openLink(intent.getData().toString());
        } else if (Intent.ACTION_SEND.equals(action)) {
            Uri uri = (Uri) intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (uri != null)
                files.add(uri);
        } else if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            ArrayList<Uri> uris = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (uris != null)
                files.addAll(uris);
        }
        if (!files.isEmpty())
            openFiles(files);
    }

    /** Models / projects from other apps (Files "open with", share, drag & drop): to file paths, then to PrusaSlicer. */
    private void openFiles(final ArrayList<Uri> uris) {
        if (sSideBar != null)
            sSideBar.showSlicing();
        new Thread(() -> {
            for (Uri uri : uris) {
                String path = "file".equals(uri.getScheme()) ? uri.getPath() : resolvePath(uri);
                if (path == null || !new File(path).canRead())
                    path = copyToCache(uri);
                if (path != null)
                    nativeOpenLink("file://" + path);
            }
        }).start();
    }

    private boolean onDrag(android.view.View v, android.view.DragEvent event) {
        switch (event.getAction()) {
            case android.view.DragEvent.ACTION_DRAG_STARTED:
                return event.getClipDescription() != null
                        && event.getClipDescription().getMimeTypeCount() > 0;
            case android.view.DragEvent.ACTION_DROP: {
                requestDragAndDropPermissions(event);
                android.content.ClipData clip = event.getClipData();
                ArrayList<Uri> uris = new ArrayList<>();
                for (int i = 0; clip != null && i < clip.getItemCount(); ++i)
                    if (clip.getItemAt(i).getUri() != null)
                        uris.add(clip.getItemAt(i).getUri());
                if (uris.isEmpty())
                    return false;
                openFiles(uris);
                return true;
            }
            default:
                return true;
        }
    }

    static void openLink(String url) {
        if (!url.startsWith("prusaslicer://"))
            return;
        Log.i(TAG, "link: " + url.replaceAll("code=[^&]*", "code=…"));
        if (sSideBar != null)
            sSideBar.showSlicing();
        nativeOpenLink(url);
    }

    // ---------------------------------------------------------------------------------------
    // Side bar (desktop "left bar")
    // ---------------------------------------------------------------------------------------

    private static SideBar sSideBar;

    /** Opens / closes PrusaSlicer's preferences dialog (AndroidApp.cpp). */
    static native void nativeTogglePreferences();
    /** Creates the Connect / Printables page (WebPages.PLACE_*) unless it exists (AndroidApp.cpp). */
    static native void nativeOpenTab(int place);
    /** Account button: Prusa Account login dialog, or log out (AndroidApp.cpp). */
    static native void nativeAccountButton();

    static void showSlicing() {
        if (sSideBar != null)
            sSideBar.showSlicing();
    }

    // ---------------------------------------------------------------------------------------
    // Web views of PrusaSlicer's browser logics (AndroidWebView.cpp <-> WebPages.java)
    // ---------------------------------------------------------------------------------------

    static native void nativeWebViewEvent(int id, int event, String a, String b);

    /** Called from native code (SDL thread). */
    public static void webCommand(final int id, final String command, final String a, final String b) {
        sInstance.runOnUiThread(() -> {
            if (sSideBar != null)
                sSideBar.webPages().command(id, command, a, b);
        });
    }

    /** Called from native code when the preferences dialog opens or closes. */
    public static void setPreferencesSelected(final boolean selected) {
        sInstance.runOnUiThread(() -> {
            if (sSideBar != null)
                sSideBar.setPreferencesSelected(selected);
        });
    }

    // ---------------------------------------------------------------------------------------
    // Touchpad gestures (Android 14+): two-finger swipe and pinch arrive as classified pointer
    // events SDL ignores. Pinch zooms through fractional wheel steps (the camera zooms by
    // 1 / (1 - 0.1 * step), see Camera::update_zoom), the two-finger swipe pans the view like
    // dragging with the middle mouse button.
    // ---------------------------------------------------------------------------------------

    private static final int SDL_ACTION_SCROLL = MotionEvent.ACTION_SCROLL;
    // Camera::update_zoom(step) zooms by 1 / (1 - 0.1 * step): 10 follows the fingers exactly.
    private static final float PINCH_GAIN = 12f;
    private boolean mPanning;
    private float mPanX, mPanY, mLastX, mLastY, mLastSpan;

    private boolean handleTouchpadGesture(MotionEvent event) {
        if (android.os.Build.VERSION.SDK_INT < 34)
            return false;
        // Web pages (Connect, Printables, login) scroll and zoom on their own.
        if (sSideBar != null && sSideBar.isWebPageShown()) {
            endPan();
            return false;
        }
        int classification = event.getClassification();
        int action = event.getActionMasked();
        if (classification != MotionEvent.CLASSIFICATION_NONE || action == MotionEvent.ACTION_SCROLL)
            javaLog("gesture cls=" + classification + " act=" + action + " src=0x" + Integer.toHexString(event.getSource())
                    + " pinch=" + event.getAxisValue(MotionEvent.AXIS_GESTURE_PINCH_SCALE_FACTOR)
                    + " sx=" + event.getAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_X_DISTANCE)
                    + " sy=" + event.getAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_Y_DISTANCE)
                    + " vs=" + event.getAxisValue(MotionEvent.AXIS_VSCROLL) + " n=" + event.getPointerCount() + " x=" + event.getX() + " y=" + event.getY());
        if (classification == MotionEvent.CLASSIFICATION_TWO_FINGER_SWIPE) {
            // The Googlebook leaves the gesture scroll axes at 0: follow the (virtual) finger position.
            // Native code scrolls ImGui lists under the pointer, or pans the 3D scene.
            float x = event.getX(), y = event.getY();
            if (action == MotionEvent.ACTION_DOWN || (action == MotionEvent.ACTION_MOVE && !mPanning)) {
                int[] origin = new int[2];
                mSurface.getLocationInWindow(origin);
                mLastX = x;
                mLastY = y;
                mPanning = true;
                nativeTouchpadSwipe(0, x - origin[0], y - origin[1], 0f, 0f);
            } else if (action == MotionEvent.ACTION_MOVE) {
                nativeTouchpadSwipe(1, 0f, 0f, x - mLastX, y - mLastY);
                mLastX = x;
                mLastY = y;
            }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL)
                endPan();
            return true;
        }
        if (classification == MotionEvent.CLASSIFICATION_PINCH) {
            endPan();
            // The reported scale factor is mostly 1.0 here: use the distance of the two fingers.
            float span = event.getPointerCount() >= 2
                    ? (float) Math.hypot(event.getX(0) - event.getX(1), event.getY(0) - event.getY(1)) : 0f;
            if (action == MotionEvent.ACTION_MOVE && span > 0f && mLastSpan > 0f && span != mLastSpan) {
                float scale = span / mLastSpan;
                SDLActivity.onNativeMouse(0, SDL_ACTION_SCROLL, 0f, PINCH_GAIN * (1f - 1f / scale), false);
            }
            mLastSpan = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) ? 0f : span;
            return true;
        }
        endPan();
        return false;
    }

    static native void nativeTouchpadSwipe(int phase, float x, float y, float dx, float dy);

    private void endPan() {
        if (!mPanning)
            return;
        mPanning = false;
        nativeTouchpadSwipe(2, 0f, 0f, 0f, 0f);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (handleTouchpadGesture(event))
            return true;
        return super.dispatchTouchEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (handleTouchpadGesture(event))
            return true;
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        // Back inside Connect / Printables navigates the web page instead of reaching PrusaSlicer.
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK && sSideBar != null) {
            if (event.getAction() == android.view.KeyEvent.ACTION_UP && sSideBar.onBackPressed())
                return true;
            if (event.getAction() == android.view.KeyEvent.ACTION_DOWN && sSideBar.isWebPageShown())
                return true;
        }
        return super.dispatchKeyEvent(event);
    }

    // ---------------------------------------------------------------------------------------
    // Resources
    // ---------------------------------------------------------------------------------------

    private void unpackResources() {
        File target = new File(getFilesDir(), "resources");
        File marker = new File(getFilesDir(), "resources.version");
        String version;
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime + "";
        } catch (Exception e) {
            version = "0";
        }
        if (target.isDirectory() && marker.isFile() && version.equals(readText(marker)))
            return;

        long start = System.currentTimeMillis();
        deleteRecursively(target);
        try {
            copyAssetDir(getAssets(), "resources", target);
            writeText(marker, version);
        } catch (IOException e) {
            Log.e(TAG, "Unpacking resources failed", e);
        }
        Log.i(TAG, "Resources unpacked in " + (System.currentTimeMillis() - start) + " ms");
    }

    private static void copyAssetDir(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            target.getParentFile().mkdirs();
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                copy(in, out);
            }
            return;
        }
        target.mkdirs();
        for (String child : children)
            copyAssetDir(assets, path + "/" + child, new File(target, child));
    }

    // ---------------------------------------------------------------------------------------
    // Storage permission: PrusaSlicer works with plain file paths
    // ---------------------------------------------------------------------------------------

    private void requestAllFilesAccess() {
        if (Environment.isExternalStorageManager())
            return;
        Toast.makeText(this, "PrusaSlicer braucht Zugriff auf Dateien, um Modelle zu öffnen und G-Code zu speichern.",
                Toast.LENGTH_LONG).show();
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    // ---------------------------------------------------------------------------------------
    // Called from native code (AndroidBridge.cpp)
    // ---------------------------------------------------------------------------------------

    public static String[] pickFilesToOpen(String title, String mimeTypes, boolean multiple) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mimeTypes.isEmpty() ? "*/*" : mimeTypes);
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
        intent.putExtra(Intent.EXTRA_TITLE, title);
        Intent result = runPicker(intent, REQUEST_OPEN);
        if (result == null)
            return new String[0];

        ArrayList<Uri> uris = new ArrayList<>();
        if (result.getClipData() != null) {
            for (int i = 0; i < result.getClipData().getItemCount(); ++i)
                uris.add(result.getClipData().getItemAt(i).getUri());
        } else if (result.getData() != null) {
            uris.add(result.getData());
        }
        ArrayList<String> paths = new ArrayList<>();
        for (Uri uri : uris) {
            String path = resolvePath(uri);
            if (path == null || !new File(path).canRead())
                path = copyToCache(uri);
            if (path != null)
                paths.add(path);
        }
        return paths.toArray(new String[0]);
    }

    public static String pickFileToSave(String title, String suggestedName, String mimeType) {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mimeType.isEmpty() ? "application/octet-stream" : mimeType);
        intent.putExtra(Intent.EXTRA_TITLE, suggestedName);
        Intent result = runPicker(intent, REQUEST_SAVE);
        if (result == null || result.getData() == null)
            return null;
        String path = resolvePath(result.getData());
        Log.i(TAG, "save: " + result.getData() + " -> " + path);
        if (path == null || !new File(path).getParentFile().canWrite()) {
            String name = displayName(result.getData());
            return exportThroughCache(result.getData(), name != null ? name : suggestedName);
        }
        return path;
    }

    public static String inputDialog(final String title, final String text, final String defaultValue) {
        final String[] answer = { null };
        final CountDownLatch latch = new CountDownLatch(1);
        sInstance.runOnUiThread(() -> {
            final EditText input = new EditText(sInstance);
            input.setInputType(InputType.TYPE_CLASS_TEXT);
            input.setText(defaultValue);
            input.setSelectAllOnFocus(true);
            new AlertDialog.Builder(sInstance)
                    .setTitle(title)
                    .setMessage(text)
                    .setView(input)
                    .setPositiveButton(android.R.string.ok, (d, w) -> answer[0] = input.getText().toString())
                    .setNegativeButton(android.R.string.cancel, null)
                    .setOnDismissListener(d -> latch.countDown())
                    .show();
        });
        await(latch);
        return answer[0];
    }

    public static int choiceDialog(final String title, final String[] values) {
        final int[] answer = { -1 };
        final CountDownLatch latch = new CountDownLatch(1);
        sInstance.runOnUiThread(() -> new AlertDialog.Builder(sInstance)
                .setTitle(title)
                .setItems(values, (d, which) -> answer[0] = which)
                .setOnDismissListener(d -> latch.countDown())
                .show());
        await(latch);
        return answer[0];
    }

    /**
     * Unsaved preset changes (AndroidDialogManager::show_unsaved_changes_dialog): the changed options
     * with check boxes. Returns 0 = transfer the checked options, 1 = discard, -1 = cancel.
     */
    public static int diffDialog(final String title, final String message, final String[] lines,
                                 final boolean canTransfer, final boolean[] checked) {
        java.util.Arrays.fill(checked, true);
        final int[] answer = { -1 };
        final CountDownLatch latch = new CountDownLatch(1);
        sInstance.runOnUiThread(() -> {
            AlertDialog.Builder builder = new AlertDialog.Builder(sInstance)
                    .setTitle(title)
                    .setMultiChoiceItems(lines, checked, (d, which, isChecked) -> checked[which] = isChecked)
                    .setNegativeButton("Verwerfen", (d, w) -> answer[0] = 1)
                    .setNeutralButton(android.R.string.cancel, (d, w) -> answer[0] = -1)
                    .setOnDismissListener(d -> latch.countDown());
            if (canTransfer)
                builder.setPositiveButton("Übernehmen", (d, w) -> answer[0] = 0);
            // An AlertDialog shows either a message or a list: the explanation goes into a custom title.
            android.widget.TextView header = new android.widget.TextView(sInstance);
            header.setText(title + "\n\n" + message);
            header.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 16);
            int pad = (int) (20 * sInstance.getResources().getDisplayMetrics().density);
            header.setPadding(pad, pad, pad, pad / 2);
            builder.setCustomTitle(header);
            builder.create().show();
        });
        await(latch);
        return answer[0];
    }

    /** Shows the folder (of a file) in the Files app. */
    public static void openFolder(String path) {
        File dir = new File(path);
        if (!dir.isDirectory())
            dir = dir.getParentFile();
        String root = Environment.getExternalStorageDirectory().getPath();
        if (dir != null && dir.getPath().startsWith(root)) {
            String relative = dir.getPath().substring(root.length()).replaceFirst("^/", "");
            Uri uri = DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:" + relative);
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setDataAndType(uri, DocumentsContract.Document.MIME_TYPE_DIR);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                sInstance.startActivity(intent);
                return;
            } catch (Exception e) {
                Log.w(TAG, "No app shows " + uri, e);
            }
        }
        showToast(path);
    }

    // Saving to places without a file path (Google Drive, other apps' providers): PrusaSlicer writes
    // into the cache, the finished file is then copied to the chosen document.
    private static final ArrayList<android.os.FileObserver> sExportObservers = new ArrayList<>();

    private static String exportThroughCache(final Uri target, String name) {
        final File dir = new File(sInstance.getCacheDir(), "exports");
        dir.mkdirs();
        final File file = new File(dir, name == null || name.isEmpty() ? "export" : name);
        file.delete();
        android.os.FileObserver observer = new android.os.FileObserver(dir,
                android.os.FileObserver.CLOSE_WRITE | android.os.FileObserver.MOVED_TO) {
            @Override
            public void onEvent(int event, String changed) {
                if (!file.getName().equals(changed))
                    return;
                try (InputStream in = new java.io.FileInputStream(file);
                     OutputStream out = sInstance.getContentResolver().openOutputStream(target, "wt")) {
                    copy(in, out);
                    showToast("Gespeichert: " + displayName(target));
                } catch (Exception e) {
                    Log.e(TAG, "Copying the export to " + target + " failed", e);
                    showToast("Speichern fehlgeschlagen: " + e.getMessage());
                }
            }
        };
        observer.startWatching();
        synchronized (sExportObservers) {
            sExportObservers.add(observer);
        }
        return file.getPath();
    }

    // ---------------------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------------------

    private static Intent runPicker(Intent intent, int requestCode) {
        sPickerLatch = new CountDownLatch(1);
        sPickerResult = null;
        sInstance.runOnUiThread(() -> {
            try {
                sInstance.startActivityForResult(intent, requestCode);
            } catch (Exception e) {
                Log.e(TAG, "Starting the file picker failed", e);
                sPickerLatch.countDown();
            }
        });
        await(sPickerLatch);
        return sPickerResult;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_OPEN || requestCode == REQUEST_SAVE) {
            sPickerResult = resultCode == RESULT_OK ? data : null;
            if (sPickerLatch != null)
                sPickerLatch.countDown();
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    /**
     * Maps a document URI to its file path: "primary:Download/a.stl", "raw:/storage/..." and the
     * Downloads provider's MediaStore ids ("msf:123"), which are looked up via their _data column.
     */
    private static String resolvePath(Uri uri) {
        try {
            String id = DocumentsContract.getDocumentId(uri);
            Log.i(TAG, "resolve document id: " + id);
            if (id.startsWith("raw:"))
                return id.substring(4);
            if (id.startsWith("msf:") || id.matches("\\d+")) {
                String mediaId = id.startsWith("msf:") ? id.substring(4) : id;
                String path = queryDataColumn(
                        android.provider.MediaStore.Files.getContentUri("external"), "_id=?", new String[] { mediaId });
                if (path == null)
                    path = queryDataColumn(uri, null, null);
                if (path == null) {
                    // DownloadManager ids ("18") are no MediaStore ids: look the file up by its name.
                    String name = displayName(uri);
                    if (name != null)
                        path = queryDataColumn(android.provider.MediaStore.Files.getContentUri("external"),
                                "_display_name=?", new String[] { name });
                }
                return path;
            }
            int colon = id.indexOf(':');
            if (colon > 0) {
                String volume = id.substring(0, colon);
                String relative = id.substring(colon + 1);
                if (volume.equalsIgnoreCase("primary"))
                    return Environment.getExternalStorageDirectory().getPath() + "/" + relative;
                return "/storage/" + volume + "/" + relative;
            }
        } catch (Exception e) {
            Log.w(TAG, "Cannot resolve " + uri, e);
        }
        return null;
    }

    private static String displayName(Uri uri) {
        try (Cursor c = sInstance.getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0))
                return c.getString(0);
        } catch (Exception e) {
            Log.w(TAG, "No display name for " + uri, e);
        }
        return null;
    }

    private static String queryDataColumn(Uri uri, String selection, String[] args) {
        // newest first: a just created document wins over an older file of the same name elsewhere
        try (Cursor c = sInstance.getContentResolver().query(uri, new String[] { "_data" }, selection, args,
                selection != null ? "date_modified DESC" : null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0))
                return c.getString(0);
        } catch (Exception e) {
            Log.w(TAG, "No _data for " + uri, e);
        }
        return null;
    }

    private static String copyToCache(Uri uri) {
        String name = "import";
        try (Cursor c = sInstance.getContentResolver().query(uri, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int index = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0)
                    name = c.getString(index);
            }
        } catch (Exception ignored) {
        }
        File dir = new File(sInstance.getCacheDir(), "imports");
        dir.mkdirs();
        File target = new File(dir, name);
        try (InputStream in = sInstance.getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(target)) {
            copy(in, out);
            return target.getPath();
        } catch (IOException e) {
            Log.e(TAG, "Copying " + uri + " failed", e);
            return null;
        }
    }

    private static void showToast(String text) {
        sInstance.runOnUiThread(() -> Toast.makeText(sInstance, text, Toast.LENGTH_LONG).show());
    }

    private static void await(CountDownLatch latch) {
        try {
            latch.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1 << 16];
        int n;
        while ((n = in.read(buffer)) > 0)
            out.write(buffer, 0, n);
    }

    private static String readText(File file) {
        try (InputStream in = new java.io.FileInputStream(file)) {
            return new String(in.readAllBytes()).trim();
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeText(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes());
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null)
            for (File child : children)
                deleteRecursively(child);
        file.delete();
    }
}

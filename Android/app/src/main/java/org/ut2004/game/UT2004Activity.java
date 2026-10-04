package org.ut2004.game;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.os.ParcelFileDescriptor;
import android.Manifest;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.InputType;
import android.view.Display;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.view.WindowInsets;
import android.widget.EditText;
import android.window.OnBackInvokedDispatcher;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;

import org.libsdl.app.SDLActivity;

public class UT2004Activity extends SDLActivity {
    private static final int PICK_ASSETS = 0x7504, ASK_NOTIFY = 0x7505, ASK_BATTERY = 0x7506;
    // An empty string means the player backed out.
    private static final LinkedBlockingQueue<String> assetSource = new LinkedBlockingQueue<>();
    private static String assetError, assetUrl;
    private static final LinkedBlockingQueue<Boolean> serverStarted = new LinkedBlockingQueue<>();
    private static String serverCmd;

    private WifiManager.MulticastLock lanLock;
    private boolean imeVisible;

    @Override
    protected String[] getLibraries() {
        // SDL is linked into libmain.so.
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        // Inis and logs go next to the extracted game, and touch is the main input.
        return new String[] { "-nohomedir", "-VIRTUALJOYSTICK" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            lanLock = wifi.createMulticastLock("UT2004 LAN");
            lanLock.acquire();
        }
        // Android keeps apps at 60 Hz unless they ask, and VSync caps the game at the panel's rate.
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode(), best = current;
        for (Display.Mode mode : display.getSupportedModes())
            if (mode.getPhysicalWidth() == current.getPhysicalWidth() && mode.getPhysicalHeight() == current.getPhysicalHeight()
                && mode.getRefreshRate() > best.getRefreshRate())
                best = mode;
        WindowManager.LayoutParams params = getWindow().getAttributes();
        params.preferredDisplayModeId = best.getModeId();
        getWindow().setAttributes(params);
        // Back and the back gesture work like Escape and open or close the menus. From Android 13 they
        // come through this callback, older versions send KEYCODE_BACK, which SDL maps the same way.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, () -> {
                // This Back closed the on-screen keyboard, SDLDummyEdit already told the game.
                if (isScreenKeyboardShown())
                    return;
                onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
                onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE);
            });
            // The back gesture and the IME's own button close it without a key event, which SDL needs to stop text input.
            getWindow().getDecorView().setOnApplyWindowInsetsListener((view, insets) -> {
                boolean visible = insets.isVisible(WindowInsets.Type.ime());
                if (imeVisible && !visible && isScreenKeyboardShown())
                    onNativeKeyboardFocusLost();
                imeVisible = visible;
                return view.onApplyWindowInsets(insets);
            });
        }
    }

    // SDLLaunch calls the methods below on its own thread while it installs the game content on first launch.

    // Returns {file descriptor, size or -1}. Downloads come through a pipe, so they unpack while they arrive.
    static long[] openAssetStream(String uri) throws IOException {
        if (uri.startsWith("content:")) {
            ParcelFileDescriptor file = mSingleton.getContentResolver().openFileDescriptor(Uri.parse(uri), "r");
            if (file == null)
                throw new IOException("Couldn't open " + uri);
            long size = file.getStatSize();
            return new long[] { file.detachFd(), size };
        }
        HttpURLConnection http = (HttpURLConnection) new URL(uri).openConnection();
        // A stalled transfer would otherwise block the launcher forever, the timeout ends it like a dropped connection.
        http.setConnectTimeout(15000);
        http.setReadTimeout(30000);
        // The progress counts the bytes as they are stored.
        http.setRequestProperty("Accept-Encoding", "identity");
        ParcelFileDescriptor[] pipe;
        try {
            if (http.getResponseCode() != HttpURLConnection.HTTP_OK)
                throw new IOException(uri + " answered " + http.getResponseCode() + " " + http.getResponseMessage());
            pipe = ParcelFileDescriptor.createPipe();
        } catch (IOException e) {
            http.disconnect();
            throw e;
        }
        new Thread(() -> {
            // A dropped connection ends the stream early, which the decoder reports as a truncated archive.
            try (InputStream in = http.getInputStream();
                 OutputStream out = new ParcelFileDescriptor.AutoCloseOutputStream(pipe[1])) {
                byte[] buf = new byte[1 << 16];
                for (int n; (n = in.read(buf)) > 0; )
                    out.write(buf, 0, n);
            } catch (IOException ignored) {
            } finally {
                http.disconnect();
            }
        }, "UT2004 download").start();
        return new long[] { pipe[0].detachFd(), http.getContentLengthLong() };
    }

    // Returns document uri and relative path pairs for every file in a picked folder, or null for anything else.
    static String[] listAssetTree(String uri) {
        Uri tree = Uri.parse(uri);
        if (!DocumentsContract.isTreeUri(tree))
            return null;
        List<String> files = new ArrayList<>();
        listTree(mSingleton.getContentResolver(), tree, DocumentsContract.getTreeDocumentId(tree), "", files);
        return files.toArray(new String[0]);
    }

    private static void listTree(ContentResolver resolver, Uri tree, String id, String prefix, List<String> files) {
        String[] columns = { DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                             DocumentsContract.Document.COLUMN_MIME_TYPE };
        try (Cursor c = resolver.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, id), columns, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String child = c.getString(0), path = prefix + c.getString(1);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) {
                    listTree(resolver, tree, child, path + "/", files);
                } else {
                    files.add(DocumentsContract.buildDocumentUriUsingTree(tree, child).toString());
                    files.add(path);
                }
            }
        }
    }

    // Waits until the player enters a URL or picks an archive or folder, null if they back out.
    static String askAssetSource(String error, String url) throws InterruptedException {
        assetError = error;
        assetUrl = url;
        UT2004Activity activity = (UT2004Activity) mSingleton;
        activity.runOnUiThread(activity::showAssetDialog);
        String source = assetSource.take();
        return source.isEmpty() ? null : source;
    }

    private void showAssetDialog() {
        EditText edit = new EditText(this);
        edit.setSingleLine();
        edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        edit.setText(assetUrl);
        new AlertDialog.Builder(this)
            .setTitle("Game content")
            .setMessage(assetError + "\n\nEnter another download URL, or pick a .tar.xz, .tar or .zip archive or a folder "
                        + "with the Maps, Textures and other content folders on this device.")
            .setView(edit)
            .setPositiveButton("Download", (d, w) -> {
                // An empty field retries the last URL, as an empty answer means backing out.
                String url = edit.getText().toString().trim();
                assetSource.add(url.isEmpty() ? assetUrl : url);
            })
            .setNeutralButton("Archive", (d, w) -> pickAssets(new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")))
            .setNegativeButton("Folder", (d, w) -> pickAssets(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)))
            .setOnCancelListener(d -> assetSource.add(""))
            .show()
            .setCanceledOnTouchOutside(false);
    }

    private void pickAssets(Intent intent) {
        try {
            startActivityForResult(intent, PICK_ASSETS);
        } catch (ActivityNotFoundException e) {
            assetError = "This device has no file picker.";
            showAssetDialog();
        }
    }

    // SDLLaunch calls this when DEDICATED closes the game. Declining still starts the server, it may just get stopped sooner.
    static void startServer(String cmd) throws InterruptedException {
        serverCmd = cmd;
        UT2004Activity activity = (UT2004Activity) mSingleton;
        activity.runOnUiThread(activity::askNotifications);
        serverStarted.take();
    }

    // The notification carries the Stop button.
    private void askNotifications() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
            && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] { Manifest.permission.POST_NOTIFICATIONS }, ASK_NOTIFY);
        else
            askBattery();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode == ASK_NOTIFY)
            askBattery();
        else
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    // Some vendors kill background services of apps that are not exempt, despite the foreground notification.
    private void askBattery() {
        if (!getSystemService(PowerManager.class).isIgnoringBatteryOptimizations(getPackageName())) {
            try {
                startActivityForResult(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                                                  Uri.parse("package:" + getPackageName())), ASK_BATTERY);
                return;
            } catch (ActivityNotFoundException ignored) {
            }
        }
        launchServer();
    }

    private void launchServer() {
        startForegroundService(new Intent(this, ServerService.class).putExtra(ServerService.EXTRA_CMD, serverCmd));
        serverStarted.add(true);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == ASK_BATTERY) {
            // After onResume, as only a visible app may start a foreground service.
            getWindow().getDecorView().post(this::launchServer);
            return;
        }
        if (requestCode != PICK_ASSETS) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        if (resultCode == RESULT_OK && data != null && data.getData() != null)
            assetSource.add(data.getData().toString());
        else
            showAssetDialog();
    }

    @Override
    protected void onDestroy() {
        // Releases the launcher if it still waits on the dialog.
        assetSource.add("");
        serverStarted.add(false);
        if (lanLock != null && lanLock.isHeld())
            lanLock.release();
        super.onDestroy();
    }
}

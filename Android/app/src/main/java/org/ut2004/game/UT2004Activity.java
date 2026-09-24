package org.ut2004.game;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.view.WindowInsets;
import android.window.OnBackInvokedDispatcher;

import org.libsdl.app.SDLActivity;

public class UT2004Activity extends SDLActivity {
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

    @Override
    protected void onDestroy() {
        if (lanLock != null && lanLock.isHeld())
            lanLock.release();
        super.onDestroy();
    }
}

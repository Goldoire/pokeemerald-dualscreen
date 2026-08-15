package com.pokeemerald.experimental;

import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import java.util.Arrays;

import org.libsdl.app.SDLActivity;

public class PokeEmeraldActivity extends SDLActivity {
    private static final long SNAPSHOT_INTERVAL_MS = 120;

    private DualScreenPresentation presentation;
    private final Handler snapshotHandler = new Handler(Looper.getMainLooper());
    private final Runnable snapshotPump = new Runnable() {
        @Override
        public void run() {
            // Self-heal: the Thor's system UI can steal the bottom display and
            // dismiss the presentation; re-show it whenever it is gone.
            if (presentation == null || !presentation.isShowing()) {
                presentation = null;
                showBottomScreen();
            }
            if (presentation != null && presentation.isShowing()) {
                String json = DualScreenBridge.nativeGetSnapshotJson();
                presentation.updateState(DualScreenState.parse(json));
            }
            // The overlay paints letterbox bars from the live setting. On a
            // release cold start DualScreen_FillAssets runs before the config
            // is read, so the first draw sees widescreen=0 and those bars
            // stick until something invalidates this view.
            if (controls != null) {
                controls.postInvalidate();
            }
            snapshotHandler.postDelayed(this, SNAPSHOT_INTERVAL_MS);
        }
    };

    private GbaControlsView controls;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        controls = new GbaControlsView(this);
        mLayout.addView(controls, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    @Override
    protected void onResume() {
        super.onResume();
        showBottomScreen();
        snapshotHandler.removeCallbacks(snapshotPump);
        snapshotHandler.postDelayed(snapshotPump, SNAPSHOT_INTERVAL_MS);
    }

    @Override
    protected void onPause() {
        snapshotHandler.removeCallbacks(snapshotPump);
        dismissBottomScreen();
        super.onPause();
    }

    private void showBottomScreen() {
        if (presentation != null && presentation.isShowing()) {
            return;
        }
        DisplayManager displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        Display[] displays = displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (displays.length == 0) {
            return; // Single-display device; game stays fullscreen.
        }
        presentation = new DualScreenPresentation(this, displays[0]);
        presentation.setSettingsListener(() -> {
            if (controls != null) {
                controls.postInvalidate();
            }
        });
        presentation.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        try {
            presentation.show();
        } catch (WindowManager.InvalidDisplayException e) {
            presentation = null;
        }
    }

    private void dismissBottomScreen() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
    }

    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // The manifest already keeps this activity in sensor landscape mode.
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) {
            return;
        }

        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && mSurface != null) {
            mSurface.post(() -> {
                int width = mSurface.getWidth();
                int height = mSurface.getHeight();
                mSurface.setSystemGestureExclusionRects(Arrays.asList(
                        new Rect(0, height / 2, width / 5, height),
                        new Rect(width * 4 / 5, height / 2, width, height)));
            });
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}

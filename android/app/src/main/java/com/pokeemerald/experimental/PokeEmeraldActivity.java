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
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.util.Arrays;

import org.libsdl.app.SDLActivity;

public class PokeEmeraldActivity extends SDLActivity {
    private int rightStickDirection = -1;
    private int rightStickDeviceId = -1;
    private final java.util.Set<Integer> bottomKeys = new java.util.HashSet<>();
    private final Handler controllerHandler = new Handler(Looper.getMainLooper());
    private final Runnable stickRepeat = new Runnable() {
        @Override public void run() {
            if (rightStickDirection < 0 || !bottomControllerEnabled()
                    || android.view.InputDevice.getDevice(rightStickDeviceId) == null) {
                stopRightStick();
                return;
            }
            presentation.navigateExtra(rightStickDirection);
            controllerHandler.postDelayed(this, 140);
        }
    };

    private boolean bottomControllerEnabled() {
        return presentation != null && presentation.isShowing()
                && presentation.extraControllerAvailable();
    }

    private void stopRightStick() {
        rightStickDirection = -1;
        controllerHandler.removeCallbacks(stickRepeat);
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        int key = event.getKeyCode();
        boolean mapped = key == android.view.KeyEvent.KEYCODE_BUTTON_L1
                || key == android.view.KeyEvent.KEYCODE_BUTTON_R1
                || key == android.view.KeyEvent.KEYCODE_BUTTON_Y;
        if (event.getAction() == android.view.KeyEvent.ACTION_UP && bottomKeys.remove(key)) return true;
        // Finish consuming a UI-owned press even if battle starts while held.
        if (event.getAction() == android.view.KeyEvent.ACTION_DOWN && bottomKeys.contains(key)) return true;
        if (mapped && bottomControllerEnabled()
                && event.getAction() == android.view.KeyEvent.ACTION_DOWN
                && event.getRepeatCount() == 0) {
            bottomKeys.add(key);
            if (key == android.view.KeyEvent.KEYCODE_BUTTON_Y)
                presentation.navigateExtra(DualScreenView.NAV_CONFIRM);
            else presentation.switchTab(key == android.view.KeyEvent.KEYCODE_BUTTON_R1 ? 1 : -1);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        if (event.getActionMasked() != android.view.MotionEvent.ACTION_MOVE)
            return super.dispatchGenericMotionEvent(event);
        if ((event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK)
                == android.view.InputDevice.SOURCE_JOYSTICK && bottomControllerEnabled()) {
            android.view.InputDevice device = event.getDevice();
            int xAxis = android.view.MotionEvent.AXIS_Z;
            int yAxis = android.view.MotionEvent.AXIS_RZ;
            if (device != null && (device.getMotionRange(xAxis, event.getSource()) == null
                    || device.getMotionRange(yAxis, event.getSource()) == null)) {
                xAxis = android.view.MotionEvent.AXIS_RX;
                yAxis = android.view.MotionEvent.AXIS_RY;
            }
            float x = event.getAxisValue(xAxis), y = event.getAxisValue(yAxis);
            float deadzone = rightStickDirection < 0 ? 0.55f : 0.35f;
            int direction = Math.max(Math.abs(x), Math.abs(y)) < deadzone ? -1
                    : Math.abs(x) > Math.abs(y)
                    ? (x < 0 ? DualScreenView.NAV_LEFT : DualScreenView.NAV_RIGHT)
                    : (y < 0 ? DualScreenView.NAV_UP : DualScreenView.NAV_DOWN);
            if (direction != rightStickDirection) {
                stopRightStick();
                rightStickDirection = direction;
                rightStickDeviceId = event.getDeviceId();
                if (direction >= 0) {
                    presentation.navigateExtra(direction);
                    controllerHandler.postDelayed(stickRepeat, 350);
                }
            }
        } else stopRightStick();
        // SDL still needs the left stick, hat and triggers in this same motion event.
        return super.dispatchGenericMotionEvent(event);
    }

    private static final long SNAPSHOT_INTERVAL_MS = 120;

    private static final int PICK_SAVE_FOLDER = 42;
    private DualScreenPresentation presentation;

    private void chooseSaveFolder() {
        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
                | android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | android.content.Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, PICK_SAVE_FOLDER);
        } catch (android.content.ActivityNotFoundException e) {
            new android.app.AlertDialog.Builder(this).setMessage("No folder picker is available on this device.")
                    .setPositiveButton("OK", null).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_SAVE_FOLDER || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        try {
            SaveFileLocation.selectFolder(this, data.getData(), data.getFlags());
            new android.app.AlertDialog.Builder(this)
                    .setMessage("Save folder selected. Save your game, then fully close and reopen the app. "
                            + "An existing .sav or .srm in that folder will be loaded and updated. "
                            + "pokeemerald.sav takes priority over pokeemerald.srm when both exist. "
                            + "If no save exists, your previous save will be copied there.")
                    .setPositiveButton("OK", null).show();
        } catch (Exception e) {
            new android.app.AlertDialog.Builder(this).setTitle("Could not select save folder")
                    .setMessage(e.getMessage()).setPositiveButton("OK", null).show();
        }
    }

    // Called by SDL on its startup thread, before the game reads flash memory.
    public int openCustomSaveFile(String defaultPath) {
        try {
            return SaveFileLocation.open(this, defaultPath);
        } catch (Exception e) {
            SaveFileLocation.recordError(this, e);
            android.util.Log.e("SaveFileLocation", "Cannot open selected save", e);
            runOnUiThread(() -> android.widget.Toast.makeText(this,
                    "Cannot open selected save folder: " + e.getMessage()
                            + ". Reopen the app to choose another folder.",
                    android.widget.Toast.LENGTH_LONG).show());
            return -2;
        }
    }

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
                if (!bottomControllerEnabled()) stopRightStick();
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

    // Button presses for an open battle takeover panel. The native side
    // queues them off the game's own input; this just hands them over,
    // faster than the snapshot pump so the panel keeps up with a held d-pad.
    private static final long NAV_INTERVAL_MS = 33;
    private final Handler navHandler = new Handler(Looper.getMainLooper());
    private final Runnable navPump = new Runnable() {
        @Override
        public void run() {
            if (presentation != null) {
                int action;
                while ((action = DualScreenBridge.nativeDrainNavKey()) >= 0) {
                    presentation.navigate(action);
                }
            }
            navHandler.postDelayed(this, NAV_INTERVAL_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Hide the bars before the first layout so SDL's SurfaceView is
        // sized to the full display, not inset and then resized.
        applyImmersiveFlags();
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
        navHandler.removeCallbacks(navPump);
        navHandler.postDelayed(navPump, NAV_INTERVAL_MS);
    }

    @Override
    protected void onPause() {
        stopRightStick();
        bottomKeys.clear();
        snapshotHandler.removeCallbacks(snapshotPump);
        navHandler.removeCallbacks(navPump);
        dismissBottomScreen();
        super.onPause();
    }

    private void applyImmersiveFlags() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // setSystemUiVisibility is deprecated from API 30 and does not stop
            // the decor insetting the content here, which is what shrank the
            // SurfaceView. setDecorFitsSystemWindows(false) is the call that
            // actually gives the content the whole window.
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
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
        presentation.setChangeSavePathListener(this::chooseSaveFolder);
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
            stopRightStick();
            return;
        }

        // Re-applied on every focus gain because IMMERSIVE_STICKY only hides
        // the bars again after the user swipes them back in.
        applyImmersiveFlags();

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

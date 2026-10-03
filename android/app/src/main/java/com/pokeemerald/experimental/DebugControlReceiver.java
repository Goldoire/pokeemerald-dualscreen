package com.pokeemerald.experimental;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.util.Log;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Lets a development PC drive the game over adb, feeding the same virtual
 * key queue the bottom screen uses (adb's own key events never reach SDL):
 *
 *   adb shell am broadcast -a com.pokeemerald.dualscreen.DEBUG_KEYS --ei mask 8 --ei frames 4
 *       hold a GBA button mask for N frames, then release
 *       (A=1 B=2 SELECT=4 START=8 RIGHT=16 LEFT=32 UP=64 DOWN=128 R=256 L=512)
 *   adb shell am broadcast -a com.pokeemerald.dualscreen.DEBUG_WARP --ei group 0 --ei num 9 --ei x 10 --ei y 10
 *       warp the player (overworld only)
 *   adb shell am broadcast -a com.pokeemerald.dualscreen.DEBUG_SETTING --ei setting 11 --ei value 1
 *       change a platform setting (persists to pokeemerald.cfg)
 *
 * Registered at runtime by PokeEmeraldActivity; the actions carry no data a
 * player could not enter with the buttons themselves.
 */
public final class DebugControlReceiver extends BroadcastReceiver {
    private static final String TAG = "SDL/APP";
    public static final String ACTION_KEYS = "com.pokeemerald.dualscreen.DEBUG_KEYS";
    public static final String ACTION_WARP = "com.pokeemerald.dualscreen.DEBUG_WARP";
    public static final String ACTION_SETTING = "com.pokeemerald.dualscreen.DEBUG_SETTING";
    public static final String ACTION_SNAPSHOT = "com.pokeemerald.dualscreen.DEBUG_SNAPSHOT";

    public static DebugControlReceiver register(Context context) {
        DebugControlReceiver receiver = new DebugControlReceiver();
        IntentFilter filter = new IntentFilter();
        filter.addAction(ACTION_KEYS);
        filter.addAction(ACTION_WARP);
        filter.addAction(ACTION_SETTING);
        if (BuildConfig.DEBUG) filter.addAction(ACTION_SNAPSHOT);
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            context.registerReceiver(receiver, filter);
        }
        Log.i(TAG, "debug control receiver registered");
        return receiver;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (action == null) {
            return;
        }
        if (BuildConfig.DEBUG && ACTION_SNAPSHOT.equals(action)) {
            // Read the same published snapshot as the real presentation UI.
            // Ordered adb broadcasts return it without writing save or game state.
            try {
                JSONObject snapshot = new JSONObject(DualScreenBridge.nativeGetSnapshotJson());
                JSONArray settings = new JSONArray();
                for (int id = 0; id <= DualScreenBridge.SETTING_VOXEL_LIGHTING; id++) {
                    settings.put(DualScreenBridge.nativeGetAppliedPlatformSetting(id));
                }
                snapshot.put("platformSettings", settings);
                snapshot.put("activeVoxelRenderer", DualScreenBridge.nativeGetActiveVoxelRenderer());
                setResultData(snapshot.toString());
            } catch (JSONException error) {
                Log.e(TAG, "Cannot read debug snapshot", error);
            }
        } else if (ACTION_KEYS.equals(action)) {
            int mask = intent.getIntExtra("mask", 0);
            int frames = Math.max(1, Math.min(60, intent.getIntExtra("frames", 4)));
            int release = Math.max(0, Math.min(60, intent.getIntExtra("release", 2)));
            int[] masks = new int[frames + release];
            for (int i = 0; i < frames; i++) {
                masks[i] = mask;
            }
            DualScreenBridge.nativeQueueKeys(masks);
            Log.i(TAG, "debug keys mask=" + mask + " frames=" + frames);
        } else if (ACTION_WARP.equals(action)) {
            int group = intent.getIntExtra("group", 0);
            int num = intent.getIntExtra("num", 0);
            int x = intent.getIntExtra("x", 0);
            int y = intent.getIntExtra("y", 0);
            DualScreenBridge.nativeDebugWarp(group, num, x, y);
            Log.i(TAG, "debug warp " + group + ":" + num + " (" + x + "," + y + ")");
        } else if (ACTION_SETTING.equals(action)) {
            int setting = intent.getIntExtra("setting", -1);
            int value = intent.getIntExtra("value", 0);
            if (setting == 100) {
                DualScreenBridge.nativeSetVoxelDebugFlags(value);
                Log.i(TAG, "debug voxel flags=" + value);
            } else if (setting >= 0) {
                DualScreenBridge.nativeSetPlatformSetting(setting, value);
                Log.i(TAG, "debug setting " + setting + "=" + value);
            }
        }
    }
}

package com.pokeemerald.experimental;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Arrays;

/**
 * Launcher gate for distributable builds: those ship libmain.so with all
 * ROM asset bytes zeroed plus assets/asset_manifest.bin. Before the game
 * can start, the user picks their own Emerald ROM; the manifest's ranges
 * are extracted into files/assets.bin, which the native side loads at
 * boot. Development builds have no manifest and pass straight through.
 */
public final class RomGateActivity extends Activity {
    private static final int PICK_ROM = 1;
    private static final int ROM_SIZE = 16 * 1024 * 1024;

    private byte[] manifest;
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        manifest = readAsset("asset_manifest.bin");
        File assetsBin = new File(getFilesDir(), "assets.bin");
        if (manifest == null || assetsBin.exists()) {
            launchGame();
            return;
        }

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.BLACK);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(18);
        status.setGravity(Gravity.CENTER);
        status.setPadding(48, 0, 48, 32);
        status.setText("This build contains no game data.\n"
                + "Select your Pokémon Emerald (USA/Europe) ROM to continue.");
        Button pick = new Button(this);
        pick.setText("Select ROM");
        pick.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK_ROM);
        });
        layout.addView(status);
        layout.addView(pick);
        setContentView(layout);
    }

    private byte[] readAsset(String name) {
        try (InputStream in = getAssets().open(name)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[65536];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        } catch (IOException e) {
            return null;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || data == null) {
            return;
        }
        Uri uri = data.getData();
        try {
            byte[] rom = readUri(uri);
            if (rom.length != ROM_SIZE) {
                status.setText("That file is not a 16MB GBA ROM. Try another file.");
                return;
            }
            byte[] sha = MessageDigest.getInstance("SHA-1").digest(rom);
            byte[] expected = Arrays.copyOfRange(manifest, 0, 20);
            if (!Arrays.equals(sha, expected)) {
                status.setText("ROM hash mismatch — this build needs the exact"
                        + " USA/Europe Emerald ROM it was made against.");
                return;
            }
            writeAssets(rom);
            launchGame();
        } catch (Exception e) {
            status.setText("Could not read that file: " + e.getMessage());
        }
    }

    private byte[] readUri(Uri uri) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream(ROM_SIZE);
            byte[] buffer = new byte[65536];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }

    private void writeAssets(byte[] rom) throws IOException {
        int count = (manifest[20] & 0xFF) | ((manifest[21] & 0xFF) << 8)
                | ((manifest[22] & 0xFF) << 16) | ((manifest[23] & 0xFF) << 24);
        File manifestOut = new File(getFilesDir(), "asset_manifest.bin");
        try (FileOutputStream out = new FileOutputStream(manifestOut)) {
            out.write(manifest);
        }
        File tmp = new File(getFilesDir(), "assets.bin.tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            int offset = 24;
            for (int i = 0; i < count; i++) {
                int size = readLe(manifest, offset + 4);
                int romOff = readLe(manifest, offset + 8);
                out.write(rom, romOff, size);
                offset += 12;
            }
        }
        if (!tmp.renameTo(new File(getFilesDir(), "assets.bin"))) {
            throw new IOException("could not finalize assets.bin");
        }
    }

    private static int readLe(byte[] data, int offset) {
        return (data[offset] & 0xFF) | ((data[offset + 1] & 0xFF) << 8)
                | ((data[offset + 2] & 0xFF) << 16) | ((data[offset + 3] & 0xFF) << 24);
    }

    private void launchGame() {
        startActivity(new Intent(this, PokeEmeraldActivity.class));
        finish();
    }
}

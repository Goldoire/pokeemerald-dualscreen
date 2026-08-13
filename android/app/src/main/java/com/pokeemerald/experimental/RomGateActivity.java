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
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Arrays;

/**
 * Launcher gate for distributable builds, which ship libmain.so with all
 * ROM asset bytes zeroed plus assets/asset_manifest.bin. The game needs
 * the user's own Emerald ROM as files/baserom.gba; it can arrive either
 * through the picker here or by dropping baserom.gba into
 * Android/data/<package>/files/ on internal storage. Once present and
 * verified, later launches skip straight to the game. Development builds
 * have no manifest and pass straight through.
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
        if (manifest == null) {
            launchGame(); // development build
            return;
        }
        try {
            writeFileIfMissing(new File(getFilesDir(), "asset_manifest.bin"), manifest);
        } catch (IOException ignored) {
        }

        File internalRom = new File(getFilesDir(), "baserom.gba");
        if (internalRom.length() == ROM_SIZE) {
            launchGame();
            return;
        }

        // Power-user path: baserom.gba dropped into Android/data/.../files/.
        File externalRom = new File(getExternalFilesDir(null), "baserom.gba");
        if (externalRom.length() == ROM_SIZE) {
            try {
                byte[] rom = readFully(new FileInputStream(externalRom));
                if (validate(rom)) {
                    writeFile(internalRom, rom);
                    launchGame();
                    return;
                }
            } catch (Exception ignored) {
            }
        }

        buildPickerUi();
    }

    private void buildPickerUi() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.BLACK);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(18);
        status.setGravity(Gravity.CENTER);
        status.setPadding(48, 0, 48, 32);
        status.setText("This build contains no game data.\n\n"
                + "Select your Pokémon Emerald (USA/Europe) ROM, or place it at\n"
                + "Android/data/" + getPackageName() + "/files/baserom.gba");
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

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || data == null) {
            return;
        }
        try {
            byte[] rom = readFully(getContentResolver().openInputStream(data.getData()));
            if (rom.length != ROM_SIZE) {
                status.setText("That file is not a 16MB GBA ROM. Try another file.");
                return;
            }
            if (!validate(rom)) {
                status.setText("ROM hash mismatch — this build needs the exact"
                        + " USA/Europe Emerald ROM it was made against.");
                return;
            }
            writeFile(new File(getFilesDir(), "baserom.gba"), rom);
            launchGame();
        } catch (Exception e) {
            status.setText("Could not read that file: " + e.getMessage());
        }
    }

    private boolean validate(byte[] rom) throws Exception {
        byte[] sha = MessageDigest.getInstance("SHA-1").digest(rom);
        return Arrays.equals(sha, Arrays.copyOfRange(manifest, 0, 20));
    }

    private byte[] readAsset(String name) {
        try (InputStream in = getAssets().open(name)) {
            return readFully(in);
        } catch (IOException e) {
            return null;
        }
    }

    private static byte[] readFully(InputStream in) throws IOException {
        try (InputStream stream = in) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[65536];
            int read;
            while ((read = stream.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }

    private static void writeFile(File file, byte[] data) throws IOException {
        File tmp = new File(file.getParentFile(), file.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
        }
        if (!tmp.renameTo(file)) {
            throw new IOException("could not finalize " + file.getName());
        }
    }

    private static void writeFileIfMissing(File file, byte[] data) throws IOException {
        if (file.length() != data.length) {
            writeFile(file, data);
        }
    }

    private void launchGame() {
        startActivity(new Intent(this, PokeEmeraldActivity.class));
        finish();
    }
}

package com.pokeemerald.experimental;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.system.Os;
import android.system.OsConstants;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;

/** SAF folders are content URIs, not filesystem paths. Native code owns the returned fd. */
final class SaveFileLocation {
    private static final String NAME = "pokeemerald.sav";

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("save_location", Context.MODE_PRIVATE);
    }

    static void selectFolder(Context context, Uri tree, int flags) throws Exception {
        int access = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        if ((flags & access) != access) throw new IOException("The folder needs read and write access.");
        context.getContentResolver().takePersistableUriPermission(tree, access);
        Uri existing = findSave(context, tree);
        if (existing != null) {
            try (ParcelFileDescriptor descriptor = context.getContentResolver().openFileDescriptor(existing, "rw")) {
                if (descriptor == null) throw new IOException("Cannot open this folder's save for reading and writing.");
                Os.lseek(descriptor.getFileDescriptor(), 0, OsConstants.SEEK_SET);
            }
        }
        if (!prefs(context).edit().putString("tree", tree.toString()).remove("error").commit())
            throw new IOException("Could not remember the folder.");
    }

    static void validateFolder(Context context) throws Exception {
        String error = prefs(context).getString("error", null);
        if (error != null) throw new IOException(error);
        String tree = prefs(context).getString("tree", null);
        if (tree != null) findSave(context, Uri.parse(tree));
    }

    static void recordError(Context context, Exception error) {
        prefs(context).edit().putString("error", String.valueOf(error.getMessage())).commit();
    }

    static void useDefault(Context context) {
        prefs(context).edit().remove("tree").remove("active_document").remove("error").commit();
    }

    private static Uri findSave(Context context, Uri tree) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree,
                DocumentsContract.getTreeDocumentId(tree));
        Uri preferredSav = null;
        Uri preferredSrm = null;
        Uri candidate = null;
        int candidates = 0;
        try (Cursor cursor = context.getContentResolver().query(children,
                new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot read the selected folder.");
            while (cursor.moveToNext()) {
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))) continue;
                String name = cursor.getString(1);
                if (name == null) continue;
                String lower = name.toLowerCase(java.util.Locale.ROOT);
                if (!lower.endsWith(".sav") && !lower.endsWith(".srm")) continue;
                Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0));
                if (NAME.equals(name)) preferredSav = document;
                if ("pokeemerald.srm".equals(name)) preferredSrm = document;
                candidate = document;
                candidates++;
            }
        }
        // Preserve the original filename preference independent of provider listing order.
        if (preferredSav != null) return preferredSav;
        if (preferredSrm != null) return preferredSrm;
        if (candidates > 1)
            throw new IOException("Multiple .sav/.srm files found. Choose a folder containing only the game's save, "
                    + "or name it pokeemerald.sav or pokeemerald.srm.");
        return candidate;
    }

    static int open(Context context, String defaultPath) throws Exception {
        SharedPreferences preferences = prefs(context);
        String selected = preferences.getString("tree", null);
        if (selected == null) return -1;
        Uri tree = Uri.parse(selected);
        Uri document = findSave(context, tree);
        boolean created = document == null;
        if (created) {
            document = DocumentsContract.createDocument(context.getContentResolver(),
                    DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree)),
                    "application/octet-stream", NAME + ".migrating-" + java.util.UUID.randomUUID());
            if (document == null) throw new IOException("Cannot create a save in that folder.");
        }
        try {
            if (created) {
                String previous = preferences.getString("active_document", null);
                File original = new File(defaultPath);
                if (previous != null || original.exists()) {
                    try (InputStream input = previous != null
                            ? context.getContentResolver().openInputStream(Uri.parse(previous))
                            : new FileInputStream(original);
                         OutputStream output = context.getContentResolver().openOutputStream(document, "wt")) {
                        if (input == null || output == null) throw new IOException("Cannot copy the previous save.");
                        byte[] buffer = new byte[8192];
                        int count;
                        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                    }
                }
            }
            if (created) {
                // Interrupted copies remain temporary files, never a seemingly valid game save.
                Uri finalized = DocumentsContract.renameDocument(context.getContentResolver(), document, NAME);
                if (finalized == null) throw new IOException("Cannot finalize the copied save.");
                document = finalized;
            }
            try (ParcelFileDescriptor descriptor = context.getContentResolver().openFileDescriptor(document, "rw")) {
                if (descriptor == null) throw new IOException("Cannot open the selected save.");
                // Cloud/stream-only providers cannot support the game's random-access flash IO.
                Os.lseek(descriptor.getFileDescriptor(), 0, OsConstants.SEEK_SET);
                if (!preferences.edit().putString("active_document", document.toString()).commit())
                    throw new IOException("Cannot remember the active save.");
                return descriptor.detachFd();
            }
        } catch (Exception e) {
            if (created) {
                try { DocumentsContract.deleteDocument(context.getContentResolver(), document); }
                catch (Exception cleanupError) { e.addSuppressed(cleanupError); }
            }
            throw e;
        }
    }
}

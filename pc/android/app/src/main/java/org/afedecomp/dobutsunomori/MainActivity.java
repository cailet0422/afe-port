package org.afedecomp.dobutsunomori;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;

import java.util.Locale;

import org.libsdl.app.SDLActivity;

/* SDL's activity runs SDL_main from libmain.so (pc/src/pc_main.c).
 *
 * The disc image can sit in any folder the player picks with the system
 * folder picker (the app can't read other folders by path), and the Korean
 * patch file comes from the system file picker. The static methods are
 * called from the game thread, see pc/src/pc_rom_dir.c. */
public class MainActivity extends SDLActivity {
    private static final int REQUEST_DISC_FOLDER = 0x4166;
    private static final int REQUEST_PATCH_FILE = 0x4167;
    private static final String PREFS = "afe";
    private static final String PREF_DISC_FOLDER = "disc_folder";

    /* 0 = idle, 1 = picker open, 2 = picked, 3 = cancelled */
    private static volatile int sPickState = 0;
    private static String sDiscFileName = "";
    private static Uri sPatchFile = null;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /* Opens the folder picker; returns 1 if it did (then poll pickState) */
    public static int pickDiscFolder() {
        final Activity activity = (Activity) getContext();

        if (activity == null) {
            return 0;
        }
        sPickState = 1;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);

                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                try {
                    activity.startActivityForResult(intent, REQUEST_DISC_FOLDER);
                } catch (ActivityNotFoundException e) {
                    sPickState = 3;
                }
            }
        });
        return 1;
    }

    /* Opens the file picker for the Korean patch (.zip or .xdelta); returns 1
     * if it did (then poll pickState, and openPatchFd once it is 2) */
    public static int pickPatchFile() {
        final Activity activity = (Activity) getContext();

        if (activity == null) {
            return 0;
        }
        sPickState = 1;
        sPatchFile = null;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);

                intent.addCategory(Intent.CATEGORY_OPENABLE);
                /* .xdelta has no MIME type, so any file */
                intent.setType("*/*");
                try {
                    activity.startActivityForResult(intent, REQUEST_PATCH_FILE);
                } catch (ActivityNotFoundException e) {
                    sPickState = 3;
                }
            }
        });
        return 1;
    }

    public static int pickState() {
        return sPickState;
    }

    /* A file descriptor for the picked patch file, or -1 */
    public static int openPatchFd() {
        Context context = getContext();

        if (context == null || sPatchFile == null) {
            return -1;
        }
        try {
            ParcelFileDescriptor fd = context.getContentResolver().openFileDescriptor(sPatchFile, "r");
            return fd != null ? fd.detachFd() : -1;
        } catch (Exception e) {
            return -1;
        }
    }

    /* The picked patch file's name, for the log */
    public static String patchFileName() {
        Context context = getContext();

        if (context == null || sPatchFile == null) {
            return "";
        }
        try (Cursor cursor = context.getContentResolver().query(sPatchFile,
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && cursor.getString(0) != null) {
                return cursor.getString(0);
            }
        } catch (Exception e) {
            // the name is only for the log
        }
        return "";
    }

    /* Starts the app again (after the Korean patch was installed): a small
     * activity in another process ends this one and opens the game anew, so
     * the native library starts from a clean state */
    public static int restartApp() {
        Activity activity = (Activity) getContext();

        if (activity == null) {
            return 0;
        }
        Intent intent = new Intent(activity, RestartActivity.class);
        intent.putExtra(RestartActivity.EXTRA_PID, android.os.Process.myPid());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            activity.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            return 0;
        }
        return 1;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_PATCH_FILE) {
            sPatchFile = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
            sPickState = sPatchFile != null ? 2 : 3;
            return;
        }
        if (requestCode != REQUEST_DISC_FOLDER) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        Uri folder = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (folder == null) {
            sPickState = 3;
            return;
        }
        /* keep the access after the app restarts */
        try {
            getContentResolver().takePersistableUriPermission(folder, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            // still readable in this run
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(PREF_DISC_FOLDER, folder.toString()).apply();
        sPickState = 2;
    }

    private static Uri discFolder() {
        Context context = getContext();
        String folder;

        if (context == null) {
            return null;
        }
        folder = context.getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_DISC_FOLDER, null);
        return folder != null ? Uri.parse(folder) : null;
    }

    private static boolean isDiscImage(String name) {
        String lower = name.toLowerCase(Locale.ROOT);

        return lower.endsWith(".iso") || lower.endsWith(".ciso") || lower.endsWith(".gcm");
    }

    /* A file descriptor for the first disc image in the picked folder, or -1 */
    public static int openDiscFd() {
        Context context = getContext();
        Uri folder = discFolder();

        if (context == null || folder == null) {
            return -1;
        }
        ContentResolver resolver = context.getContentResolver();
        try {
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(folder,
                    DocumentsContract.getTreeDocumentId(folder));
            Cursor cursor = resolver.query(children, new String[] {
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            }, null, null, null);

            if (cursor == null) {
                return -1;
            }
            try {
                while (cursor.moveToNext()) {
                    String name = cursor.getString(1);

                    if (name == null || !isDiscImage(name)) {
                        continue;
                    }
                    Uri document = DocumentsContract.buildDocumentUriUsingTree(folder, cursor.getString(0));
                    ParcelFileDescriptor fd = resolver.openFileDescriptor(document, "r");
                    if (fd != null) {
                        sDiscFileName = name;
                        return fd.detachFd();
                    }
                }
            } finally {
                cursor.close();
            }
        } catch (Exception e) {
            // the folder was removed or the access was taken back
        }
        return -1;
    }

    public static String discFileName() {
        return sDiscFileName;
    }

    /* The picked folder for the settings menu, e.g. "Download/AC"; "" if none */
    public static String discFolderName() {
        Uri folder = discFolder();

        if (folder == null) {
            return "";
        }
        try {
            String id = DocumentsContract.getTreeDocumentId(folder);
            int colon = id.indexOf(':');
            String path = colon >= 0 ? id.substring(colon + 1) : id;

            return path.isEmpty() ? id : path;
        } catch (Exception e) {
            return "";
        }
    }
}

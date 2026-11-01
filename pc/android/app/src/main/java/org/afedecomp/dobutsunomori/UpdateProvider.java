package org.afedecomp.dobutsunomori;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/* Hands the downloaded update (Updater.java) to the system's installer: since
 * Android 7 an app can't give another one a file path, only a content:// URI
 * it grants. Serves that one file, read-only, and only to whoever the
 * installer intent was granted to (not exported). */
public class UpdateProvider extends ContentProvider {
    static Uri uri(Context context) {
        return Uri.parse("content://" + context.getPackageName() + ".update/" + Updater.APK_NAME);
    }

    private File apk(Uri uri) throws FileNotFoundException {
        Context context = getContext();

        if (context == null || !("/" + Updater.APK_NAME).equals(uri.getPath())) {
            throw new FileNotFoundException(uri.toString());
        }
        return Updater.apkFile(context);
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(apk(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        return "application/vnd.android.package-archive";
    }

    /* Some installers ask for the name and size first */
    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        MatrixCursor cursor = new MatrixCursor(new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE });

        try {
            cursor.addRow(new Object[] { Updater.APK_NAME, apk(uri).length() });
        } catch (FileNotFoundException e) {
            // empty
        }
        return cursor;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }
}

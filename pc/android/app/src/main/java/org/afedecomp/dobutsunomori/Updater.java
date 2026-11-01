package org.afedecomp.dobutsunomori;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.util.TypedValue;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Locale;

/* Updates the app from the project's GitHub releases (the PC builds do the
 * same in pc/src/pc_update.c).
 *
 * When the game starts, a thread asks GitHub for the releases. If one is
 * newer than this app and has an APK, a dialog offers it: Update downloads the
 * APK (checked against the release's SHA256SUMS.txt) and hands it to the
 * system's installer, which keeps the saves and the Korean patch (they are in
 * the app's data folder). Only builds signed with the release key look, as
 * only those can install the releases over themselves; settings.ini
 * auto_update = 0 (Settings > Gameplay > Updates) turns it off, and "Skip
 * this version" stops it asking about that version. */
final class Updater {
    private static final String RELEASES_URL = "https://api.github.com/repos/cailet0422/afe-port/releases?per_page=10";
    private static final String ASSET_SUFFIX = "-Android.apk";
    private static final String SUMS_NAME = "SHA256SUMS.txt";
    /* README.md: the certificate the releases are signed with */
    private static final String RELEASE_CERT_SHA256 =
        "db6fc1ad60b6b6d980bee0990f120ef481f195f117729fa194b124d14752e8b7";
    private static final String PREFS = "afe";
    private static final String PREF_SKIP = "update_skip";
    static final String APK_NAME = "update.apk";
    private static final int MAX_LIST = 4 << 20;
    private static final int MAX_SUMS = 64 << 10;

    private static final class Release {
        String tag = "";
        String apkName = "";
        String apkUrl = "";
        long apkSize = 0;
        String sumsUrl = "";
    }

    private static volatile boolean sCancel = false;

    private Updater() {}

    /* From MainActivity.onCreate */
    static void checkInBackground(final Activity activity) {
        final Context app = activity.getApplicationContext();

        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    if (!isReleaseBuild(app) || !autoUpdateOn(app)) {
                        return;
                    }
                    final String current = versionName(app);
                    final Release release = newestRelease(current);
                    String skipped = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(PREF_SKIP, "");

                    if (release == null || release.tag.equals(skipped)) {
                        return;
                    }
                    activity.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (!activity.isFinishing()) {
                                offer(activity, release, current);
                            }
                        }
                    });
                } catch (Throwable e) {
                    // no network, GitHub's limit, anything: the game goes on
                }
            }
        }, "update_check").start();
    }

    /* --- what to update to ----------------------------------------------- */

    @SuppressWarnings("deprecation")
    private static boolean isReleaseBuild(Context context) throws Exception {
        PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(),
                                                                      PackageManager.GET_SIGNATURES);

        if (info.signatures == null) {
            return false;
        }
        for (Signature signature : info.signatures) {
            if (hex(MessageDigest.getInstance("SHA-256").digest(signature.toByteArray())).equals(RELEASE_CERT_SHA256)) {
                return true;
            }
        }
        return false;
    }

    /* settings.ini in the game's folder (pc_android.c), "auto_update = 0" */
    private static boolean autoUpdateOn(Context context) {
        File dir = context.getExternalFilesDir(null);
        File ini = new File(dir != null ? dir : context.getFilesDir(), "settings.ini");

        if (!ini.exists()) {
            return true;
        }
        try (BufferedReader reader = new BufferedReader(new FileReader(ini))) {
            String line;

            while ((line = reader.readLine()) != null) {
                String[] parts = line.split("=", 2);

                if (parts.length == 2 && parts[0].trim().equals("auto_update")) {
                    return !parts[1].trim().equals("0");
                }
            }
        } catch (IOException e) {
            // the default
        }
        return true;
    }

    private static String versionName(Context context) throws Exception {
        String name = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;

        return name != null ? name : "";
    }

    private static Release newestRelease(String current) throws Exception {
        JSONArray list = new JSONArray(new String(get(RELEASES_URL, true, MAX_LIST, 0, null), "UTF-8"));
        Release best = null;

        for (int i = 0; i < list.length(); i++) {
            JSONObject entry = list.optJSONObject(i);
            JSONArray assets;
            Release r = new Release();

            if (entry == null || entry.optBoolean("draft", false)) {
                continue;
            }
            r.tag = entry.optString("tag_name", "");
            assets = entry.optJSONArray("assets");
            for (int j = 0; assets != null && j < assets.length(); j++) {
                JSONObject asset = assets.optJSONObject(j);
                String name = asset != null ? asset.optString("name", "") : "";

                if (name.equals(SUMS_NAME)) {
                    r.sumsUrl = asset.optString("browser_download_url", "");
                } else if (r.apkUrl.isEmpty() && name.endsWith(ASSET_SUFFIX)) {
                    r.apkName = name;
                    r.apkUrl = asset.optString("browser_download_url", "");
                    r.apkSize = asset.optLong("size", 0);
                }
            }
            if (r.apkUrl.isEmpty() || compareVersions(r.tag, current) <= 0) {
                continue;
            }
            if (best == null || compareVersions(r.tag, best.tag) > 0) {
                best = r;
            }
        }
        return best;
    }

    /* As pc_update_version_compare: "v0.1.8" > "0.1.7", "0.1.8-rc1" < "0.1.8" */
    static int compareVersions(String a, String b) {
        long[] pa = new long[4], pb = new long[4];
        int ra = parseVersion(a, pa), rb = parseVersion(b, pb);

        if (ra < 0 || rb < 0) {
            return (ra < 0 ? 0 : 1) - (rb < 0 ? 0 : 1);
        }
        for (int i = 0; i < 4; i++) {
            if (pa[i] != pb[i]) {
                return pa[i] < pb[i] ? -1 : 1;
            }
        }
        return rb - ra;
    }

    /* -1: not a version; else 1 for a test build ("-rc1"), 0 for a release */
    private static int parseVersion(String s, long[] parts) {
        int i = 0, k = 0;

        if (s == null) {
            return -1;
        }
        if (s.startsWith("v") || s.startsWith("V")) {
            i = 1;
        }
        if (i >= s.length() || !Character.isDigit(s.charAt(i))) {
            return -1;
        }
        while (k < 4) {
            long v = 0;

            while (i < s.length() && Character.isDigit(s.charAt(i))) {
                v = v * 10 + (s.charAt(i++) - '0');
            }
            parts[k++] = v;
            if (i + 1 < s.length() && s.charAt(i) == '.' && Character.isDigit(s.charAt(i + 1))) {
                i++;
            } else {
                break;
            }
        }
        return i < s.length() && s.charAt(i) == '-' ? 1 : 0;
    }

    /* --- the dialogs ------------------------------------------------------ */

    private static void offer(final Activity activity, final Release release, String current) {
        new AlertDialog.Builder(activity, AlertDialog.THEME_DEVICE_DEFAULT_DARK)
            .setTitle("새 버전 " + release.tag)
            .setMessage("새 버전이 나왔습니다. 지금 업데이트할까요? (지금 버전: v" + current + ")\n\n"
                        + "세이브와 한글 패치는 그대로 남습니다. 설치하는 동안 게임이 닫히니, "
                        + "진행 중이면 먼저 저장하세요.\n\n"
                        + "A new version is out. Saves and the Korean patch are kept; "
                        + "the game closes while it installs.")
            .setPositiveButton("업데이트", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    download(activity, release);
                }
            })
            .setNegativeButton("나중에", null)
            .setNeutralButton("이 버전 건너뛰기", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .putString(PREF_SKIP, release.tag).apply();
                }
            })
            .show();
    }

    private static void download(final Activity activity, final Release release) {
        int pad = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 20,
                                                  activity.getResources().getDisplayMetrics());
        LinearLayout layout = new LinearLayout(activity);
        final TextView text = new TextView(activity);
        final ProgressBar bar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);

        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(pad, pad, pad, pad);
        text.setText("내려받는 중… 0%");
        bar.setMax(1000);
        layout.addView(text);
        layout.addView(bar);

        sCancel = false;
        final AlertDialog dialog = new AlertDialog.Builder(activity, AlertDialog.THEME_DEVICE_DEFAULT_DARK)
            .setTitle(release.tag + " 내려받기")
            .setView(layout)
            .setNegativeButton("취소", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    sCancel = true;
                }
            })
            .setCancelable(false)
            .show();

        new Thread(new Runnable() {
            @Override
            public void run() {
                String error = null;
                File apk = null;

                try {
                    apk = fetchApk(activity, release, new Progress() {
                        @Override
                        public void at(final long done, final long total) {
                            activity.runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    int permille = total > 0 ? (int) Math.min(1000, done * 1000 / total) : 0;

                                    bar.setProgress(permille);
                                    text.setText("내려받는 중… " + permille / 10 + "%");
                                }
                            });
                        }
                    });
                } catch (Throwable e) {
                    error = sCancel ? null : (e.getMessage() != null ? e.getMessage() : e.toString());
                }
                final String failure = error;
                final File file = apk;

                activity.runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (dialog.isShowing()) {
                            dialog.dismiss();
                        }
                        if (file != null) {
                            install(activity, file);
                        } else if (failure != null) {
                            new AlertDialog.Builder(activity, AlertDialog.THEME_DEVICE_DEFAULT_DARK)
                                .setTitle("업데이트하지 못했습니다")
                                .setMessage(failure + "\n\ngithub.com/cailet0422/afe-port/releases")
                                .setPositiveButton("확인", null)
                                .show();
                        }
                    }
                });
            }
        }, "update_download").start();
    }

    /* Android 7 and later take the APK from UpdateProvider; before that the
     * installer reads it as a file, so it goes to the shared cache folder */
    static File apkFile(Context context) {
        File dir = null;

        if (Build.VERSION.SDK_INT < 24) {
            dir = context.getExternalCacheDir();
        }
        if (dir == null) {
            dir = context.getCacheDir();
        }
        return new File(dir, APK_NAME);
    }

    private static void install(Activity activity, File apk) {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        Uri uri = Build.VERSION.SDK_INT >= 24 ? UpdateProvider.uri(activity) : Uri.fromFile(apk);

        intent.setDataAndType(uri, "application/vnd.android.package-archive");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            activity.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            new AlertDialog.Builder(activity, AlertDialog.THEME_DEVICE_DEFAULT_DARK)
                .setTitle("설치 화면을 열지 못했습니다")
                .setMessage("github.com/cailet0422/afe-port/releases 에서 APK를 받아 설치해 주세요.")
                .setPositiveButton("확인", null)
                .show();
        }
    }

    /* --- downloading ------------------------------------------------------ */

    private interface Progress {
        void at(long done, long total);
    }

    private static File fetchApk(Context context, Release release, Progress progress) throws Exception {
        String want = null;
        File apk = apkFile(context);
        File part = new File(apk.getPath() + ".part");

        if (!release.sumsUrl.isEmpty()) {
            want = findSum(new String(get(release.sumsUrl, false, MAX_SUMS, 0, null), "UTF-8"), release.apkName);
        }
        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        long size = 0;

        try (OutputStream out = new FileOutputStream(part)) {
            size = download(release.apkUrl, out, sha, release.apkSize, progress);
        }
        if (release.apkSize > 0 && size != release.apkSize) {
            part.delete();
            throw new IOException("내려받은 파일이 완전하지 않습니다 (download incomplete)");
        }
        if (want != null && !hex(sha.digest()).equals(want)) {
            part.delete();
            throw new IOException("내려받은 파일이 손상됐습니다 (SHA-256 differs)");
        }
        apk.delete();
        if (!part.renameTo(apk)) {
            throw new IOException("cannot keep the download");
        }
        if (Build.VERSION.SDK_INT < 24) {
            apk.setReadable(true, false); /* for the installer, another app */
        }
        return apk;
    }

    private static String findSum(String sums, String name) {
        for (String line : sums.split("\n")) {
            String[] parts = line.trim().split("\\s+", 2);

            if (parts.length == 2 && parts[0].length() == 64) {
                String file = parts[1].startsWith("*") ? parts[1].substring(1) : parts[1];

                if (file.trim().equals(name)) {
                    return parts[0].toLowerCase(Locale.ROOT);
                }
            }
        }
        return null;
    }

    private static HttpURLConnection open(String url, boolean api) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();

        c.setConnectTimeout(20000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true); /* GitHub sends the files from another host (https too) */
        c.setRequestProperty("User-Agent", "DobutsuNoMoriEPlus-Android (updater)");
        c.setRequestProperty("Accept", api ? "application/vnd.github+json" : "*/*");
        if (c.getResponseCode() != HttpURLConnection.HTTP_OK) {
            int code = c.getResponseCode();

            c.disconnect();
            throw new IOException("HTTP " + code);
        }
        return c;
    }

    private static byte[] get(String url, boolean api, int max, long expected, Progress progress) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();

        download(url, out, null, expected, progress);
        if (out.size() > max) {
            throw new IOException("too big");
        }
        return out.toByteArray();
    }

    private static long download(String url, OutputStream out, MessageDigest sha, long expected, Progress progress)
        throws IOException {
        HttpURLConnection c = open(url, url.startsWith("https://api.github.com/"));
        long done = 0, total = expected > 0 ? expected : c.getContentLength();
        long last = 0;
        byte[] buf = new byte[64 * 1024];

        try (InputStream in = c.getInputStream()) {
            int n;

            while ((n = in.read(buf)) > 0) {
                if (sCancel) {
                    throw new IOException("cancelled");
                }
                out.write(buf, 0, n);
                if (sha != null) {
                    sha.update(buf, 0, n);
                }
                done += n;
                if (progress != null && (done - last > 256 * 1024 || done == total)) {
                    last = done;
                    progress.at(done, total);
                }
            }
        } finally {
            c.disconnect();
        }
        return done;
    }

    private static String hex(byte[] bytes) {
        StringBuilder s = new StringBuilder(bytes.length * 2);

        for (byte b : bytes) {
            s.append(String.format(Locale.ROOT, "%02x", b & 0xFF));
        }
        return s.toString();
    }
}

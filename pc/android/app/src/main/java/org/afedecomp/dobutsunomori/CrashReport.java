package org.afedecomp.dobutsunomori;

import android.annotation.TargetApi;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/* How the last run ended, for LaunchActivity to show before the game starts.
 *
 * Three sources, so a crash anywhere shows up:
 * - crash.txt in the data folder: a native crash in the game (pc_android.c)
 *   or a Java exception (installHandler);
 * - the system's record of the process's exit (Android 11+), which also has
 *   crashes before the game's own handler is installed, for instance while
 *   the native library loads; on Android 12+ a native crash comes with its
 *   tombstone (signal, call stack, the process's log lines);
 * - the end of log.txt, the game's log of that run. */
final class CrashReport {
    private static final String PREFS = "afe";
    private static final String PREF_LAST_EXIT = "last_exit_time";
    private static boolean sHandlerInstalled = false;

    private CrashReport() {
    }

    /* The folder the game runs in (pc_android_prepare) */
    static File dataFolder(Context context) {
        File dir = context.getExternalFilesDir(null);

        return dir != null ? dir : context.getFilesDir();
    }

    /* A Java exception that ends the app is written to crash.txt */
    static synchronized void installHandler(Context context) {
        if (sHandlerInstalled) {
            return;
        }
        sHandlerInstalled = true;

        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        final File crash = new File(dataFolder(context), "crash.txt");

        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread thread, Throwable e) {
                try (PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(crash), StandardCharsets.UTF_8))) {
                    out.println("Java exception in thread " + thread.getName());
                    e.printStackTrace(out);
                } catch (Throwable ignored) {
                    // nothing more to do
                }
                if (previous != null) {
                    previous.uncaughtException(thread, e);
                }
            }
        });
    }

    /* The report on the last run if it crashed, or null. Each crash is
     * reported once: crash.txt becomes crash_prev.txt (which keeps the whole
     * report), and the time of the last exit shown is remembered. */
    static String collect(Context context) {
        File dir = dataFolder(context);
        File crash = new File(dir, "crash.txt");
        StringBuilder text = new StringBuilder();
        boolean found = false;

        if (crash.isFile()) {
            text.append("--- crash.txt ---\n").append(readLines(crash, 200));
            crash.delete();
            found = true;
        }
        if (Build.VERSION.SDK_INT >= 30) {
            String exit = lastExit(context);

            if (exit != null) {
                text.append(exit);
                found = true;
            }
        }
        if (!found) {
            return null;
        }

        StringBuilder report = new StringBuilder();
        String version = "?";

        try {
            version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
        } catch (Exception e) {
            // only for the report
        }
        report.append("version ").append(version).append(", Android ").append(Build.VERSION.RELEASE)
            .append(" (API ").append(Build.VERSION.SDK_INT).append("), ").append(Build.MANUFACTURER)
            .append(' ').append(Build.MODEL).append(", ").append(Build.SUPPORTED_ABIS[0]).append('\n');
        report.append(text);
        report.append("--- log.txt ---\n").append(readLines(new File(dir, "log.txt"), 40));

        try (Writer out = new OutputStreamWriter(new FileOutputStream(new File(dir, "crash_prev.txt")), StandardCharsets.UTF_8)) {
            out.write(report.toString());
        } catch (Exception e) {
            // the dialog still shows it
        }
        return report.toString();
    }

    /* The system's record of the newest exit not reported yet, if it was a
     * crash, an ANR or an exit() with an error status */
    @TargetApi(30)
    private static String lastExit(Context context) {
        ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long shown = prefs.getLong(PREF_LAST_EXIT, 0);
        List<ApplicationExitInfo> exits;
        ApplicationExitInfo crash = null;
        long newest = shown;

        if (am == null) {
            return null;
        }
        try {
            exits = am.getHistoricalProcessExitReasons(context.getPackageName(), 0, 8);
        } catch (Exception e) {
            return null;
        }
        for (ApplicationExitInfo exit : exits) {
            if (exit.getTimestamp() <= shown) {
                continue;
            }
            newest = Math.max(newest, exit.getTimestamp());
            if (crash == null && isCrash(exit)) {
                crash = exit; /* the list is newest first */
            }
        }
        if (newest != shown) {
            prefs.edit().putLong(PREF_LAST_EXIT, newest).apply();
        }
        if (crash == null) {
            return null;
        }

        StringBuilder text = new StringBuilder();
        text.append("--- exit ").append(reasonName(crash.getReason()))
            .append(", status ").append(crash.getStatus())
            .append(", ").append(new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date(crash.getTimestamp())))
            .append(", process ").append(crash.getProcessName()).append(" ---\n");
        if (crash.getDescription() != null) {
            text.append(crash.getDescription()).append('\n');
        }
        try (InputStream in = crash.getTraceInputStream()) {
            if (in != null) {
                byte[] trace = readAll(in, 8 << 20);

                if (crash.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE) {
                    text.append(Tombstone.describe(trace));
                } else {
                    text.append(firstLines(new String(trace, StandardCharsets.UTF_8), 80));
                }
            }
        } catch (Throwable e) {
            text.append("(trace: ").append(e).append(")\n");
        }
        return text.toString();
    }

    @TargetApi(30)
    private static boolean isCrash(ApplicationExitInfo exit) {
        switch (exit.getReason()) {
        case ApplicationExitInfo.REASON_CRASH:
        case ApplicationExitInfo.REASON_CRASH_NATIVE:
        case ApplicationExitInfo.REASON_ANR:
        case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
            return true;
        case ApplicationExitInfo.REASON_EXIT_SELF:
            return exit.getStatus() != 0;
        default:
            return false;
        }
    }

    @TargetApi(30)
    private static String reasonName(int reason) {
        switch (reason) {
        case ApplicationExitInfo.REASON_CRASH:
            return "CRASH (Java)";
        case ApplicationExitInfo.REASON_CRASH_NATIVE:
            return "CRASH_NATIVE";
        case ApplicationExitInfo.REASON_ANR:
            return "ANR";
        case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
            return "INITIALIZATION_FAILURE";
        case ApplicationExitInfo.REASON_EXIT_SELF:
            return "EXIT_SELF";
        default:
            return "reason " + reason;
        }
    }

    private static byte[] readAll(InputStream in, int limit) throws java.io.IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[16384];
        int n;

        while (out.size() < limit && (n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
        }
        return out.toByteArray();
    }

    private static String firstLines(String text, int count) {
        StringBuilder out = new StringBuilder();
        int start = 0;

        for (int i = 0; i < count && start < text.length(); i++) {
            int end = text.indexOf('\n', start);

            if (end < 0) {
                end = text.length();
            }
            out.append(text, start, end).append('\n');
            start = end + 1;
        }
        return out.toString();
    }

    /* The last lines of a text file */
    static String readLines(File file, int count) {
        ArrayDeque<String> lines = new ArrayDeque<String>();
        StringBuilder text = new StringBuilder();
        String line;

        try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            while ((line = in.readLine()) != null) {
                if (lines.size() == count) {
                    lines.removeFirst();
                }
                lines.addLast(line);
            }
        } catch (Exception e) {
            return "(" + file.getName() + ": " + e.getMessage() + ")\n";
        }
        for (String l : lines) {
            text.append(l).append('\n');
        }
        return text.toString();
    }
}

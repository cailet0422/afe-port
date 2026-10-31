package org.afedecomp.dobutsunomori;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.View;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/* The launcher's entry. Before the game (MainActivity, which loads the native
 * library) starts, it shows how the last run ended if it crashed
 * (CrashReport): a crash while the library loads, or before the game's own
 * handler is installed, would otherwise leave nothing to go on. */
public class LaunchActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        CrashReport.installHandler(this);

        String report = null;
        try {
            report = CrashReport.collect(this);
        } catch (Throwable e) {
            report = null; /* never keep the game from starting */
        }
        if (report == null) {
            startGame();
            return;
        }
        showReport(report);
    }

    private void startGame() {
        Intent intent = new Intent(this, MainActivity.class);

        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
        finish();
    }

    private void showReport(final String report) {
        int pad = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 16, getResources().getDisplayMetrics());
        TextView text = new TextView(this);
        ScrollView scroll = new ScrollView(this);

        text.setText("아래 내용을 복사해 이슈로 보내 주시면 원인을 찾는 데 도움이 됩니다. "
                     + "(Copy this into a bug report.)\n\n" + report);
        text.setTextIsSelectable(true);
        text.setTypeface(Typeface.MONOSPACE);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        text.setPadding(pad, pad, pad, pad);
        scroll.addView(text);

        final AlertDialog dialog = new AlertDialog.Builder(this, AlertDialog.THEME_DEVICE_DEFAULT_DARK)
            .setTitle("지난번 실행이 비정상 종료됐습니다")
            .setView(scroll)
            .setPositiveButton("복사", null)
            .setNegativeButton("게임 시작", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    startGame();
                }
            })
            .setNeutralButton("닫기", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    finish();
                }
            })
            .setCancelable(false)
            .create();
        dialog.show();
        /* Copying keeps the dialog open */
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);

                if (clipboard != null) {
                    clipboard.setPrimaryClip(ClipData.newPlainText("crash report", report));
                    Toast.makeText(LaunchActivity.this, "복사했습니다", Toast.LENGTH_SHORT).show();
                }
            }
        });
    }
}

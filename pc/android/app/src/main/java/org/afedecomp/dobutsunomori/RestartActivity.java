package org.afedecomp.dobutsunomori;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/* Runs in its own process (AndroidManifest.xml): ends the game's process,
 * whose native state can't be reset in place, and opens the game again.
 * Started by MainActivity.restartApp(). */
public class RestartActivity extends Activity {
    static final String EXTRA_PID = "pid";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        int pid = getIntent().getIntExtra(EXTRA_PID, -1);
        if (pid > 0) {
            Process.killProcess(pid);
        }
        Intent intent = new Intent(this, MainActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(intent);
        finish();
        Runtime.getRuntime().exit(0);
    }
}

// SPDX-License-Identifier: MIT
// RestartActivity - the app's restart (2026-11-13). The old way (an AlarmManager PendingIntent a moment
// ahead, then exit) is a background activity start for Android 14+ and was blocked on the S23 (Android 17:
// "Background activity launch blocked", the app just closed - driver switch, saves import, RESTART NOW). Now the
// running game starts this activity in its own process (":restart") while it is still in the foreground (allowed),
// then exits; this one waits for the old process to be gone, starts PortActivity from the foreground and ends.
// The same scheme as the ProcessPhoenix library (Jake Wharton, Apache-2.0) - written anew, no code taken.
package xboxrecomp.port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class RestartActivity extends Activity {
    static final String EXTRA_PID = "pid";

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        int pid = getIntent().getIntExtra(EXTRA_PID, -1);
        if (pid > 0) {
            android.os.Process.killProcess(pid);                 // same uid: allowed; a no-op once it has exited
            java.io.File proc = new java.io.File("/proc/" + pid);
            for (int i = 0; i < 100 && proc.exists(); i++) {     // up to 2 s
                try { Thread.sleep(20); } catch (InterruptedException e) { break; }
            }
        }
        Intent start = new Intent(this, PortActivity.class);
        start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(start);
        finish();
        Runtime.getRuntime().exit(0);
    }
}

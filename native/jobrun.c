/* jobrun.c - guest-side job queue for the test rig (never shipped to players).
 *
 * the run script starts this once after boot. It polls PROGDIR:jobs/next every 0.5 s; when the host
 * drops a script there (written as next.tmp and renamed, so it is complete), jobrun renames it
 * to running, runs it as an AmigaDOS script with output to PROGDIR:jobs/<n>.log, then writes
 * PROGDIR:jobs/<n>.done holding the return code and deletes running. PROGDIR:jobs/heartbeat gets the
 * poll counter every ~10 s, so the host can tell a live guest from a hung one without a
 * screenshot. PROGDIR:jobs/quit ends the loop.
 *
 * A PROGRAM instead of a script loop: Execute + deleting the script breaks Skip BACK
 * (AmiSC probe/jobrun.c, 90-testowanie-emulatory.md). */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

static int exists(const char *p)
{
	BPTR l = Lock((STRPTR)p, ACCESS_READ);
	if (l) { UnLock(l); return 1; }
	return 0;
}

static void write_text(const char *path, const char *text)
{
	BPTR f = Open((STRPTR)path, MODE_NEWFILE);
	if (f) { Write(f, (APTR)text, strlen(text)); Close(f); }
}

int main(void)
{
	unsigned long polls = 0, jobs = 0;
	char cmd[256], logp[64], donep[64], txt[64];
	BPTR d = CreateDir((STRPTR)"PROGDIR:jobs");
	if (d) UnLock(d);
	DeleteFile((STRPTR)"PROGDIR:jobs/running");
	DeleteFile((STRPTR)"PROGDIR:jobs/quit");
	write_text("PROGDIR:jobs/jobrun.up", "jobrun up\n");
	for (;;) {
		if (exists("PROGDIR:jobs/quit")) break;
		if (exists("PROGDIR:jobs/next") && Rename((STRPTR)"PROGDIR:jobs/next", (STRPTR)"PROGDIR:jobs/running")) {
			BPTR out;
			LONG rc;
			jobs++;
			snprintf(logp, sizeof logp, "PROGDIR:jobs/job%lu.log", jobs);
			snprintf(donep, sizeof donep, "PROGDIR:jobs/job%lu.done", jobs);
			snprintf(cmd, sizeof cmd, "Execute PROGDIR:jobs/running");
			out = Open((STRPTR)logp, MODE_NEWFILE);
			rc = SystemTags((STRPTR)cmd, SYS_Output, out, SYS_Input, 0, NP_StackSize, 200000, TAG_DONE);
			if (out) Close(out);
			DeleteFile((STRPTR)"PROGDIR:jobs/running");
			snprintf(txt, sizeof txt, "rc %ld\n", (long)rc);
			write_text(donep, txt);
			snprintf(txt, sizeof txt, "%lu\n", jobs);
			write_text("PROGDIR:jobs/last", txt);
		}
		if ((polls++ % 20) == 0) {
			snprintf(txt, sizeof txt, "%lu %lu\n", polls, jobs);
			write_text("PROGDIR:jobs/heartbeat", txt);
		}
		Delay(25);
	}
	write_text("PROGDIR:jobs/jobrun.down", "jobrun stopped\n");
	return 0;
}

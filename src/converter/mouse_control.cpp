// mouse_control.cpp - reads a one-byte state stream and drives the mouse.
// Windows only. Press ESC to quit.
// Build: g++ mouse_control.cpp -o mouse_control.exe -luser32 -static

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static const char *PORT = "\\\\.\\COM4";
static const int   BAUD = 115200;

// Speed is now pixels per second, independent of tick rate. Changing TICK_MS
// changes smoothness only - the cursor still travels at the same speed.
static const double SPEED_PPS = 250.0;
static const DWORD  TICK_MS   = 4;

struct State {
    int up = 0, down = 0, left = 0, right = 0, lc = 0, rc = 0;
};

static HANDLE openSerial(const char *port, int baud) {
    HANDLE h = CreateFileA(port, GENERIC_READ, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Could not open %s (error %lu)\n", port, GetLastError());
        return h;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        printf("GetCommState failed\n");
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;

    // Leave DTR deasserted so opening the port does not reset the board.
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(h, &dcb)) {
        printf("SetCommState failed\n");
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    // Fully non-blocking: return immediately with whatever is buffered, even
    // if that is nothing. The waitable timer owns the loop's timing, not this.
    COMMTIMEOUTS to = {};
    to.ReadIntervalTimeout        = MAXDWORD;
    to.ReadTotalTimeoutConstant   = 0;
    to.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR);
    return h;
}

// Frame bytes carry bit 7; anything else is noise and gets skipped.
static void unpackState(uint8_t b, State &s) {
    s.up    = (b >> 0) & 1;
    s.down  = (b >> 1) & 1;
    s.left  = (b >> 2) & 1;
    s.right = (b >> 3) & 1;
    s.lc    = (b >> 4) & 1;
    s.rc    = (b >> 5) & 1;
}

static void mouseMove(int dx, int dy) {
    INPUT in = {};
    in.type       = INPUT_MOUSE;
    in.mi.dx      = dx;
    in.mi.dy      = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

static void mouseButton(DWORD flag) {
    INPUT in = {};
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof(in));
}

int main() {
    HANDLE h = openSerial(PORT, BAUD);
    if (h == INVALID_HANDLE_VALUE) return 1;

    // GetTickCount resolves to ~15.6ms, far too coarse to pace a 4ms loop.
    // A high-resolution timer fires on schedule and sleeps in between.
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                       CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!timer) {
        printf("Could not create timer\n");
        CloseHandle(h);
        return 1;
    }

    // Cheap insurance against the scheduler preempting us mid-tick. The process
    // sleeps almost all the time, so this costs nothing elsewhere.
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)TICK_MS * 10000;   // negative = relative, 100ns units
    SetWaitableTimer(timer, &due, TICK_MS, nullptr, nullptr, FALSE);

    printf("Connected to %s. Press ESC to quit.\n", PORT);

    char chunk[256];
    State cur;
    bool leftWasDown = false, rightWasDown = false;

    // Carried fractional pixels. Without this, a 250 px/s target at 250Hz has
    // to round every step to 1 or 0, and the rounding pattern is visible.
    double accX = 0.0, accY = 0.0;
    const double tickSeconds = TICK_MS / 1000.0;

    while (true) {
        WaitForSingleObject(timer, INFINITE);

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) break;

        DWORD got = 0;
        if (!ReadFile(h, chunk, sizeof(chunk), &got, nullptr)) {
            printf("Read failed (error %lu) - was the board unplugged?\n",
                   GetLastError());
            break;
        }

        for (DWORD i = 0; i < got; i++) {
            uint8_t b = (uint8_t)chunk[i];
            if (!(b & 0x80)) continue;

            unpackState(b, cur);

            if (cur.lc && !leftWasDown)       mouseButton(MOUSEEVENTF_LEFTDOWN);
            else if (!cur.lc && leftWasDown)  mouseButton(MOUSEEVENTF_LEFTUP);

            if (cur.rc && !rightWasDown)      mouseButton(MOUSEEVENTF_RIGHTDOWN);
            else if (!cur.rc && rightWasDown) mouseButton(MOUSEEVENTF_RIGHTUP);

            leftWasDown  = cur.lc;
            rightWasDown = cur.rc;
        }

        double vx = 0.0, vy = 0.0;
        if (cur.left)  vx -= SPEED_PPS;
        if (cur.right) vx += SPEED_PPS;
        if (cur.up)    vy -= SPEED_PPS;
        if (cur.down)  vy += SPEED_PPS;

        // Diagonals would otherwise travel at sqrt(2) x SPEED_PPS. Scale the
        // vector back to unit length so speed is the same in every direction.
        if (vx != 0.0 && vy != 0.0) {
            const double INV_SQRT2 = 0.70710678;
            vx *= INV_SQRT2;
            vy *= INV_SQRT2;
        }

        accX += vx * tickSeconds;
        accY += vy * tickSeconds;

        int dx = (int)accX;
        int dy = (int)accY;
        accX -= dx;
        accY -= dy;

        if (dx != 0 || dy != 0) mouseMove(dx, dy);

        // Drop the remainder on release so it cannot leak into the next press.
        if (vx == 0.0) accX = 0.0;
        if (vy == 0.0) accY = 0.0;
    }

    if (leftWasDown)  mouseButton(MOUSEEVENTF_LEFTUP);
    if (rightWasDown) mouseButton(MOUSEEVENTF_RIGHTUP);

    CancelWaitableTimer(timer);
    CloseHandle(timer);
    CloseHandle(h);
    printf("Stopped.\n");
    return 0;
}
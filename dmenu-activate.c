/* dmenu-activate.c — High-performance EWMH window activator
 * Sends _NET_ACTIVE_WINDOW ClientMessage to the root window.
 * Dependency: libX11 only. Zero runtime overhead.
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
	if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		fprintf(stderr, "Usage: %s <window_xid>\n", argv[0]);
		return 1;
	}

	char *endptr = NULL;
	unsigned long win_id = strtoul(argv[1], &endptr, 0);
	if (*endptr != '\0' || win_id == 0) {
		fprintf(stderr, "dmenu-activate: invalid window id '%s'\n", argv[1]);
		return 1;
	}

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "dmenu-activate: failed to open X display\n");
		return 1;
	}

	Window root = DefaultRootWindow(dpy);
	Atom net_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

	XEvent xev;
	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.type = ClientMessage;
	xev.xclient.display = dpy;
	xev.xclient.window = (Window)win_id;
	xev.xclient.message_type = net_active;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = 2; /* 2 = source indication: pager */
	xev.xclient.data.l[1] = CurrentTime;
	xev.xclient.data.l[2] = 0;
	xev.xclient.data.l[3] = 0;
	xev.xclient.data.l[4] = 0;

	long mask = SubstructureNotifyMask | SubstructureRedirectMask;
	Status status = XSendEvent(dpy, root, False, mask, &xev);
	XSync(dpy, False);
	XCloseDisplay(dpy);

	return (status != 0) ? 0 : 1;
}

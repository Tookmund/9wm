/* Copyright (c) 2014 multiple authors, see README for licence details */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "dat.h"
#include "fns.h"

Client	*clients;
Client	*current;

void
setactive(c, on)
Client *c;
int on;
{
	if (c->parent == c->screen->root) {
		fprintf(stderr, "9wm: bad parent in setactive; dumping core\n");
		abort();
	}
	if (on) {
		XUngrabButton(dpy, AnyButton, AnyModifier, c->parent);
		XSetInputFocus(dpy, c->window, RevertToPointerRoot, timestamp());
		if (c->proto & Ptakefocus)
			sendcmessage(c->window, wm_protocols, wm_take_focus, 0);
		cmapfocus(c);
	}
	else
		XGrabButton(dpy, AnyButton, AnyModifier, c->parent, False,
			ButtonMask, GrabModeAsync, GrabModeSync, None, None);
	draw_border(c, on);
}

void
draw_border(c, active)
Client *c;
int active;
{
	XSetWindowBackground(dpy, c->parent, active ? c->screen->black : c->screen->white);
	XClearWindow(dpy, c->parent);
	if (c->hold && active)
		XDrawRectangle(dpy, c->parent, c->screen->gc,
				INSET, INSET, c->dx+BORDER-INSET, c->dy+BORDER-INSET);
}

void
active(c)
Client *c;
{
	Client *cc;

	if (c == 0) {
		fprintf(stderr, "9wm: active(c==0)\n");
		return;
	}
	if (c == current)
		return;
	if (current) {
		setactive(current, 0);
		if (current->screen != c->screen)
			cmapnofocus(current->screen);
	}
	setactive(c, 1);
	for (cc = clients; cc; cc = cc->next)
		if (cc->revert == c)
			cc->revert = c->revert;
	c->revert = current;
	while (c->revert && !normal(c->revert))
		c->revert = c->revert->revert;
	current = c;
#ifdef	DEBUG
	if (debug)
		dump_revert();
#endif
}

void
nofocus()
{
	static Window w = 0;
	int mask;
	XSetWindowAttributes attr;
	Client *c;

	if (current) {
		setactive(current, 0);
		for (c = current->revert; c; c = c->revert)
			if (normal(c)) {
				active(c);
				return;
			}
		cmapnofocus(current->screen);
		/* if no candidates to revert to, fall through */
	}
	current = 0;
	if (w == 0) {
		mask = CWOverrideRedirect;
		attr.override_redirect = 1;
		w = XCreateWindow(dpy, screens[0].root, 0, 0, 1, 1, 0,
			CopyFromParent, InputOnly, CopyFromParent, mask, &attr);
		XMapWindow(dpy, w);
	}
	XSetInputFocus(dpy, w, RevertToPointerRoot, timestamp());
}

void
top(c)
Client *c;
{
	Client **l, *cc;

	l = &clients;
	for (cc = *l; cc; cc = *l) {
		if (cc == c) {
			*l = c->next;
			c->next = clients;
			clients = c;
			return;
		}
		l = &cc->next;
	}
	fprintf(stderr, "9wm: %p not on client list in top()\n", c);
}

Client *
getclient(w, create)
Window w;
int create;
{
	Client *c;

	if (w == 0 || getscreen(w))
		return 0;

	for (c = clients; c; c = c->next)
		if (c->window == w || c->parent == w)
			return c;

	if (!create)
		return 0;

	c = (Client *)malloc(sizeof(Client));
	memset(c, 0, sizeof(Client));
	c->window = w;
	/* c->parent will be set by the caller */
	c->parent = None;
	c->reparenting = 0;
	c->state = WithdrawnState;
	c->init = 0;
	c->cmap = None;
	c->label = c->class = 0;
	c->revert = 0;
	c->is9term = 0;
	c->hold = 0;
	c->ncmapwins = 0;
	c->cmapwins = 0;
	c->wmcmaps = 0;
	c->next = clients;
	clients = c;
	return c;
}

void
rmclient(c)
Client *c;
{
	Client *cc;

	for (cc = current; cc && cc->revert; cc = cc->revert)
		if (cc->revert == c)
			cc->revert = cc->revert->revert;

	if (c == clients)
		clients = c->next;
	for (cc = clients; cc && cc->next; cc = cc->next)
		if (cc->next == c)
			cc->next = cc->next->next;

	if (hidden(c))
		unhidec(c, 0);

	if (c->parent != c->screen->root)
		XDestroyWindow(dpy, c->parent);

	c->parent = c->window = None;		/* paranoia */
	if (current == c) {
		current = c->revert;
		if (current == 0)
			nofocus();
		else {
			if (current->screen != c->screen)
				cmapnofocus(c->screen);
			setactive(current, 1);
		}
	}
	if (c->ncmapwins != 0) {
		XFree((char *)c->cmapwins);
		free((char *)c->wmcmaps);
	}
	if (c->iconname != 0)
		XFree((char*) c->iconname);
	if (c->name != 0)
		XFree((char*) c->name);
	if (c->instance != 0)
		XFree((char*) c->instance);
	if (c->class != 0)
		XFree((char*) c->class);
	memset(c, 0, sizeof(Client));		/* paranoia */
	free(c);
}

#ifdef	DEBUG
void
dump_revert()
{
	Client *c;
	int i;

	i = 0;
	for (c = current; c; c = c->revert) {
		fprintf(stderr, "%s(%x:%d)", c->label ? c->label : "?", c->window, c->state);
		if (i++ > 100)
			break;
		if (c->revert)
			fprintf(stderr, " -> ");
	}
	if (current == 0)
		fprintf(stderr, "empty");
	fprintf(stderr, "\n");
}

void
dump_clients()
{
	Client *c;

	for (c = clients; c; c = c->next)
		fprintf(stderr, "w 0x%x parent 0x%x @ (%d, %d)\n", c->window, c->parent, c->x, c->y);
}
#endif

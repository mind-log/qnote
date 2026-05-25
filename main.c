#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#define MAX_LINES 256
#define MAX_CHARS 512
#define PAD 8
#define WIN_W 640
#define WIN_H 280
#define CONF_DIR "/.config/qnote"
#define CONF_FILE "config"
#define DEF_PATH "~/.qnote"

typedef struct {
  char lines[MAX_LINES][MAX_CHARS];
  int count, cx, cy, scroll;
} Buf;

typedef struct {
  KeySym sym;
  unsigned int mod;
  char str[32];
} Hotkey;

static Display *dpy;
static int scr;
static Window root;
static XftFont *font;
static XftColor fg, bg, cur_clr;
static int cw, ch, lh, rows, cols;
static Atom wm_del;
static Hotkey hk;
static char *home, *jpath;

// ── Helpers ──────────────────────────────────────────────────────────

static void die(const char *s) { fprintf(stderr, "qnote: %s\n", s); exit(1); }

static char *homedir(void) {
  if (!home) { home = getenv("HOME"); if (!home) die("$HOME not set"); }
  return home;
}

static char *expand(const char *p) {
  static char buf[2048];
  if (p[0] == '~' && p[1] == '/')
    snprintf(buf, sizeof(buf), "%s%s", homedir(), p + 1);
  else
    snprintf(buf, sizeof(buf), "%s", p);
  return buf;
}

static void mkpath(const char *p) {
  struct stat st;
  if (stat(p, &st) < 0) mkdir(p, 0755);
}

static void jmkdir(void) {
  if (!jpath) { char *e = expand(DEF_PATH); jpath = strdup(e); }
  mkpath(jpath);
}

static void jfile(char *buf, int len) {
  jmkdir();
  time_t t = time(NULL); struct tm *tm = localtime(&t);
  snprintf(buf, len, "%s/%04d-%02d-%02d.md", jpath, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
}

// ── Config ───────────────────────────────────────────────────────────

static const char *mn[] = {"super","ctrl","alt","shift",NULL};
static const unsigned int mm[] = {Mod4Mask,ControlMask,Mod1Mask,ShiftMask};

static unsigned int mod_mask(const char *s) {
  for (int i = 0; mn[i]; i++) if (strcasecmp(s, mn[i]) == 0) return mm[i];
  return 0;
}

static KeySym key_sym(const char *s) {
  if (strlen(s) == 1 && s[0] >= 'a' && s[0] <= 'z') return XK_a + (s[0] - 'a');
  if (strlen(s) == 1 && s[0] >= '0' && s[0] <= '9') return XK_0 + (s[0] - '0');
  struct { char *n; KeySym s; } k[] = {
    {"return",XK_Return},{"space",XK_space},{"tab",XK_Tab},{"escape",XK_Escape},
    {"backspace",XK_BackSpace},{"delete",XK_Delete},{"left",XK_Left},
    {"right",XK_Right},{"up",XK_Up},{"down",XK_Down},{"home",XK_Home},
    {"end",XK_End},{NULL,NoSymbol}
  };
  for (int i = 0; k[i].n; i++) if (strcasecmp(s, k[i].n) == 0) return k[i].s;
  return NoSymbol;
}

static void parse_hotkey(const char *s, Hotkey *h) {
  char buf[64]; snprintf(buf, sizeof(buf), "%s", s);
  h->mod = 0; h->sym = NoSymbol; h->str[0] = 0;
  char *tok = strtok(buf, "+-");
  while (tok) {
    while (*tok == ' ') tok++;
    unsigned int m = mod_mask(tok);
    if (m) h->mod |= m;
    else { KeySym k = key_sym(tok); if (k != NoSymbol) h->sym = k; }
    tok = strtok(NULL, "+-");
  }
  snprintf(h->str, sizeof(h->str), "%s", s);
}

static void load_config(void) {
  char p[2048]; snprintf(p, sizeof(p), "%s%s/%s", homedir(), CONF_DIR, CONF_FILE);
  FILE *f = fopen(p, "r");
  if (f) {
    char line[512];
    while (fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\n")] = 0;
      if (strncmp(line, "hotkey=", 7) == 0) parse_hotkey(line + 7, &hk);
      if (strncmp(line, "path=", 5) == 0) {
        char *e = expand(line + 5);
        if (jpath) free(jpath);
        jpath = strdup(e);
      }
    }
    fclose(f);
  }
  if (hk.sym == NoSymbol) parse_hotkey("super+n", &hk);
  if (!jpath) jpath = strdup(expand(DEF_PATH));
}

static void save_config(void) {
  char d[2048]; snprintf(d, sizeof(d), "%s%s", homedir(), CONF_DIR);
  mkpath(d);
  char p[2048]; snprintf(p, sizeof(p), "%s/%s", d, CONF_FILE);
  FILE *f = fopen(p, "w");
  if (!f) return;
  fprintf(f, "hotkey=%s\n", hk.str);
  fprintf(f, "path=%s\n", jpath ? jpath : expand(DEF_PATH));
  fclose(f);
}

// ── X11 Init ─────────────────────────────────────────────────────────

static void x11_init(void) {
  dpy = XOpenDisplay(NULL);
  if (!dpy) die("cannot open display");
  scr = DefaultScreen(dpy);
  root = RootWindow(dpy, scr);
  wm_del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  font = XftFontOpenName(dpy, scr, "monospace-13");
  if (!font) font = XftFontOpenName(dpy, scr, "fixed");
  if (!font) die("no font");
  cw = font->max_advance_width; if (!cw) cw = 8;
  ch = font->ascent + font->descent;
  lh = ch + 2;
  rows = (WIN_H - PAD*2) / lh;
  cols = (WIN_W - PAD*2) / cw;
  Visual *v = DefaultVisual(dpy, scr);
  Colormap cm = DefaultColormap(dpy, scr);
  XftColorAllocName(dpy, v, cm, "#1e1e2e", &bg);
  XftColorAllocName(dpy, v, cm, "#cdd6f4", &fg);
  XftColorAllocName(dpy, v, cm, "#f5e0dc", &cur_clr);
}

// ── Hotkey registration ─────────────────────────────────────────────

static void reg_hotkey(int on) {
  KeyCode kc = XKeysymToKeycode(dpy, hk.sym);
  if (!kc) return;
  unsigned int mods[] = {0,LockMask,Mod2Mask,LockMask|Mod2Mask};
  for (int i = 0; i < 4; i++) {
    if (on) XGrabKey(dpy, kc, hk.mod | mods[i], root, False, GrabModeAsync, GrabModeAsync);
    else XUngrabKey(dpy, kc, hk.mod | mods[i], root);
  }
}

// ── Window ───────────────────────────────────────────────────────────

static Window make_win(void) {
  XSetWindowAttributes a = {.background_pixel = 0, .event_mask = ExposureMask|KeyPressMask|StructureNotifyMask};
  Window w = XCreateWindow(dpy, root, 0, 0, WIN_W, WIN_H, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel|CWEventMask, &a);
  XSetWMProtocols(dpy, w, &wm_del, 1);
  XSizeHints sh; memset(&sh, 0, sizeof(sh));
  sh.flags = PMinSize|PMaxSize; sh.min_width = sh.max_width = WIN_W; sh.min_height = sh.max_height = WIN_H;
  XSetWMNormalHints(dpy, w, &sh);
  XStoreName(dpy, w, "qnote");
  XMapWindow(dpy, w);
  Screen *s = XScreenOfDisplay(dpy, scr);
  XMoveWindow(dpy, w, (WidthOfScreen(s)-WIN_W)/2, (HeightOfScreen(s)-WIN_H)/2);
  XRaiseWindow(dpy, w);
  for (;;) {
    XEvent e; XWindowEvent(dpy, w, StructureNotifyMask, &e);
    if (e.type == MapNotify) break;
  }
  XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
  XFlush(dpy);
  return w;
}

// ── Buffer ───────────────────────────────────────────────────────────

static void binit(Buf *b) { memset(b, 0, sizeof(*b)); b->count = 1; }

static void binsert(Buf *b, char c) {
  if (!b->count) binit(b);
  char *line = b->lines[b->cy];
  int len = strlen(line);
  if (len >= MAX_CHARS-1) return;
  memmove(line + b->cx + 1, line + b->cx, len - b->cx + 1);
  line[b->cx] = c; b->cx++;
}

static void benter(Buf *b) {
  if (b->count >= MAX_LINES) return;
  char *tail = b->lines[b->cy] + b->cx;
  int tlen = strlen(tail) + 1;
  for (int i = b->count; i > b->cy; i--) memcpy(b->lines[i], b->lines[i-1], MAX_CHARS);
  memcpy(b->lines[b->cy+1], tail, tlen > MAX_CHARS ? MAX_CHARS : tlen);
  b->lines[b->cy][b->cx] = 0;
  b->count++; b->cy++; b->cx = 0;
}

static void bback(Buf *b) {
  if (b->cx > 0) {
    char *line = b->lines[b->cy];
    memmove(line + b->cx - 1, line + b->cx, strlen(line) - b->cx + 1);
    b->cx--;
  } else if (b->cy > 0) {
    int pl = strlen(b->lines[b->cy-1]);
    int sl = strlen(b->lines[b->cy]);
    b->cx = pl;
    memcpy(b->lines[b->cy-1] + pl, b->lines[b->cy], sl + 1);
    for (int i = b->cy; i < b->count-1; i++) memcpy(b->lines[i], b->lines[i+1], MAX_CHARS);
    memset(b->lines[b->count-1], 0, MAX_CHARS);
    b->count--; b->cy--;
  }
}

static void bdel(Buf *b) {
  char *line = b->lines[b->cy]; int len = strlen(line);
  if (b->cx < len) memmove(line + b->cx, line + b->cx + 1, len - b->cx);
  else if (b->cy < b->count - 1) {
    int sl = strlen(b->lines[b->cy+1]);
    memcpy(line + len, b->lines[b->cy+1], sl + 1);
    for (int i = b->cy+1; i < b->count-1; i++) memcpy(b->lines[i], b->lines[i+1], MAX_CHARS);
    memset(b->lines[b->count-1], 0, MAX_CHARS);
    b->count--;
  }
}

static void btrim(Buf *b) {
  while (b->count > 0) {
    char *last = b->lines[b->count-1];
    int i = strlen(last) - 1;
    while (i >= 0 && (last[i] == '\n' || last[i] == '\r' || last[i] == ' ' || last[i] == '\t')) i--;
    if (i < 0) { memset(last, 0, MAX_CHARS); b->count--; }
    else { last[i+1] = 0; break; }
  }
  if (!b->count) binit(b);
}

// ── Render ───────────────────────────────────────────────────────────

static XftDraw *draw;
static GC gc;

static void render(Window win, Buf *b) {
  if (!draw) {
    draw = XftDrawCreate(dpy, win, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr));
    gc = XCreateGC(dpy, win, 0, NULL);
  }
  XSetForeground(dpy, gc, 0);
  XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);
  int max_vis = b->count < rows ? b->count : rows;
  if (b->cy < b->scroll) b->scroll = b->cy;
  if (b->cy >= b->scroll + rows) b->scroll = b->cy - rows + 1;
  if (b->scroll < 0) b->scroll = 0;
  for (int i = 0; i < max_vis; i++) {
    int li = b->scroll + i;
    if (li >= b->count) break;
    int y = PAD + i * lh + font->ascent;
    XftDrawStringUtf8(draw, &fg, font, PAD, y, (unsigned char*)b->lines[li], strlen(b->lines[li]));
  }
  int cur_y = b->cy - b->scroll;
  if (cur_y >= 0 && cur_y < rows) {
    int rx = PAD + b->cx * cw;
    int ry = PAD + cur_y * lh;
    XSetForeground(dpy, gc, cur_clr.pixel);
    XFillRectangle(dpy, win, gc, rx - 1, ry, 2, lh);
  }
  XFlush(dpy);
}

// ── Save ─────────────────────────────────────────────────────────────

static void save_buf(Buf *b) {
  btrim(b);
  if (!b->count || (b->count == 1 && !b->lines[0][0])) return;
  jmkdir();
  char p[2048]; jfile(p, sizeof(p));
  FILE *f = fopen(p, "a");
  if (!f) { fprintf(stderr, "qnote: cannot write %s\n", p); return; }
  time_t t = time(NULL); struct tm *tm = localtime(&t);
  fprintf(f, "- %02d:%02d | %s\n", tm->tm_hour, tm->tm_min, b->lines[0]);
  for (int i = 1; i < b->count; i++)
    fprintf(f, "  %s\n", b->lines[i]);
  fclose(f);
}

// ── Key handling ─────────────────────────────────────────────────────

enum { SAVE = -1, CANCEL = -2 };

static int handle_key(Buf *b, XEvent *ev) {
  KeySym ks = XLookupKeysym(&ev->xkey, 0);
  int mod = ev->xkey.state;
  if (mod & ControlMask) {
    if (ks == XK_s || ks == XK_S) return SAVE;
    if (ks == XK_c || ks == XK_C) return CANCEL;
    if (ks == XK_u || ks == XK_U) { binit(b); return 0; }
  }
  if (ks == XK_Return || ks == XK_KP_Enter) { benter(b); return 0; }
  if (ks == XK_BackSpace) { bback(b); return 0; }
  if (ks == XK_Delete) { bdel(b); return 0; }
  if (ks == XK_Left) { if (b->cx > 0) b->cx--; else if (b->cy > 0) { b->cy--; b->cx = strlen(b->lines[b->cy]); } return 0; }
  if (ks == XK_Right) { if (b->cx < (int)strlen(b->lines[b->cy])) b->cx++; else if (b->cy < b->count-1) { b->cy++; b->cx = 0; } return 0; }
  if (ks == XK_Up) { if (b->cy > 0) { b->cy--; if (b->cx > (int)strlen(b->lines[b->cy])) b->cx = strlen(b->lines[b->cy]); } return 0; }
  if (ks == XK_Down) { if (b->cy < b->count-1) { b->cy++; if (b->cx > (int)strlen(b->lines[b->cy])) b->cx = strlen(b->lines[b->cy]); } return 0; }
  if (ks == XK_Home) { b->cx = 0; return 0; }
  if (ks == XK_End) { b->cx = strlen(b->lines[b->cy]); return 0; }
  if (ks == XK_Tab) { for (int i = 0; i < 4; i++) binsert(b, ' '); return 0; }
  if (ks == XK_Escape) return CANCEL;
  char buf[32] = {0};
  int len = XLookupString(&ev->xkey, buf, sizeof(buf)-1, &ks, NULL);
  for (int i = 0; i < len; i++)
    if (buf[i] >= 32 && buf[i] <= 126) binsert(b, buf[i]);
  if (hk.sym && ev->xkey.keycode == XKeysymToKeycode(dpy, hk.sym)) {
    unsigned int m = ev->xkey.state & (Mod4Mask|ControlMask|Mod1Mask|ShiftMask);
    if (m == hk.mod) return 0;
  }
  return 0;
}

// ── GUI ──────────────────────────────────────────────────────────────

static void run_note(void) {
  Window w = make_win();
  Buf b; binit(&b);
  int running = 1;
  while (running) {
    XEvent ev;
    XNextEvent(dpy, &ev);
    if (ev.type == Expose && ev.xexpose.count == 0) render(w, &b);
    else if (ev.type == KeyPress) {
      int r = handle_key(&b, &ev);
      render(w, &b);
      if (r == SAVE) {
        save_buf(&b);
        if (b.lines[0][0]) {
          XSetForeground(dpy, gc, 0);
          XFillRectangle(dpy, w, gc, 0, 0, WIN_W, WIN_H);
          XftDrawStringUtf8(draw, &fg, font, WIN_W/2-36, WIN_H/2+ch/2, (unsigned char*)"Saved!", 6);
          XFlush(dpy); usleep(300000);
        }
        running = 0;
      } else if (r == CANCEL) running = 0;
    } else if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_del) {
      save_buf(&b); running = 0;
    }
  }
  if (draw) { XftDrawDestroy(draw); draw = NULL; XFreeGC(dpy, gc); }
  XDestroyWindow(dpy, w);
  XFlush(dpy);
}

// ── CLI commands ─────────────────────────────────────────────────────

static void print_help(void) {
  printf("qnote - lightweight X11 note-taker\n\n");
  printf("Usage: qnote [OPTION]\n\n");
  printf("  (no option)     Open note window (one-shot mode)\n");
  printf("  --help          Show this help\n");
  printf("  --all           Show all qnotes taken\n");
  printf("  --today         Show today's qnotes\n");
  printf("  --delete        Interactive menu to delete a qnote\n");
  printf("  --search <txt>  Search qnotes by keyword\n");
  printf("  --setup         Interactive hotkey and path config\n");
  printf("  --listen        Daemon mode (register global hotkey)\n\n");
  printf("Controls (in GUI):\n");
  printf("  Ctrl+S          Save and exit\n");
  printf("  Ctrl+C / Esc    Cancel and exit\n");
  printf("  Ctrl+U          Clear current buffer\n\n");
  printf("Journal: %s\n", jpath ? jpath : expand(DEF_PATH));
  printf("Config:  ~%s/%s\n", CONF_DIR, CONF_FILE);
}

static void list_all(int limit) {
  jmkdir();
  char cmd[4096];
  snprintf(cmd, sizeof(cmd),
    "for f in $(ls -t '%s'/*.md 2>/dev/null); do "
    "  awk 'BEGIN{ORS=\"\"}"
    "    /^- [0-9][0-9]:[0-9][0-9] [|]/{if(e)d[++n]=e; e=$0 RS; next}"
    "    {e=e $0 RS}"
    "    END{d[++n]=e; for(i=n;i>0;i--) printf \"%%s\", d[i]}"
    "  ' \"$f\"; "
    "done", jpath);
  if (limit) {
    char tail[128]; snprintf(tail, sizeof(tail), " | head -n %d", limit);
    strncat(cmd, tail, sizeof(cmd) - strlen(cmd) - 1);
  }
  system(cmd);
}

static void show_today(void) {
  jmkdir();
  time_t t = time(NULL); struct tm *tm = localtime(&t);
  char p[2048]; snprintf(p, sizeof(p), "%s/%04d-%02d-%02d.md", jpath, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
  char cmd[4096];
  snprintf(cmd, sizeof(cmd),
    "awk 'BEGIN{ORS=\"\"}"
    "  /^- [0-9][0-9]:[0-9][0-9] [|]/{if(e)d[++n]=e; e=$0 RS; next}"
    "  {e=e $0 RS}"
    "  END{d[++n]=e; for(i=n;i>0;i--) printf \"%%s\", d[i]}"
    "' '%s' 2>/dev/null || echo 'No entries for today.'", p);
  system(cmd);
}

static void search(const char *q) {
  jmkdir();
  char cmd[2048];
  snprintf(cmd, sizeof(cmd), "grep -rn -i '%s' '%s/' 2>/dev/null", q, jpath);
  int r = system(cmd);
  if (r) printf("No matches.\n");
}

static int is_entry_start(const char *line) {
  if (line[0] != '-' || line[1] != ' ') return 0;
  if (strlen(line) < 7) return 0;
  return (line[2] >= '0' && line[2] <= '9');
}

typedef struct { char file[1024], fname[256], preview[128]; int start, end; } Ent;

static void delete_entry(void) {
  jmkdir();
  Ent all[512]; int total = 0;
  DIR *d = opendir(jpath);
  if (!d) { printf("No notes found.\n"); return; }
  struct dirent *de;
  while ((de = readdir(d)) && total < 512) {
    if (de->d_type != DT_REG || !strstr(de->d_name, ".md")) continue;
    char fp[2048]; snprintf(fp, sizeof(fp), "%s/%s", jpath, de->d_name);
    FILE *f = fopen(fp, "r"); if (!f) continue;
    char line[1024]; int ln = 1, in = 0;
    while (fgets(line, sizeof(line), f) && total < 512) {
      line[strcspn(line, "\n")] = 0;
      if (is_entry_start(line)) {
        if (in) all[total-1].end = ln - 1;
        snprintf(all[total].file, sizeof(all[total].file), "%s", fp);
        snprintf(all[total].fname, sizeof(all[total].fname), "%s", de->d_name);
        all[total].start = ln; all[total].end = ln;
        const char *ts = line;
        while (*ts && *ts != '|') ts++;
        if (*ts == '|') { ts++; while (*ts == ' ') ts++; }
        snprintf(all[total].preview, sizeof(all[total].preview), "%.127s", ts);
        total++; in = 1;
      }
      ln++;
    }
    if (in) all[total-1].end = ln - 1;
    fclose(f);
  }
  closedir(d);
  if (!total) { printf("No notes found.\n"); return; }
  printf("qnotes:\n---\n");
  for (int i = total - 1; i >= 0; i--) {
    int n = total - i;
    printf("%3d: [%s] %s\n", n, all[i].fname, all[i].preview);
  }
  printf("\nEntry to delete (0 to cancel): "); fflush(stdout);
  char buf[16]; if (!fgets(buf, sizeof(buf), stdin)) return;
  int choice = atoi(buf);
  if (choice <= 0 || choice > total) return;
  int idx = total - choice;
  char cmd[2048];
  snprintf(cmd, sizeof(cmd), "sed -i '%d,%dd' '%s'", all[idx].start, all[idx].end, all[idx].file);
  if (system(cmd) == 0) printf("Deleted entry %d.\n", choice);
  else printf("Failed to delete.\n");
}

static void setup(void) {
  hk.sym = NoSymbol; load_config();
  printf("qnote setup\n---\n");
  printf("Global hotkey [%s]: ", hk.str); fflush(stdout);
  char buf[64] = "";
  if (fgets(buf, sizeof(buf), stdin)) buf[strcspn(buf, "\n")] = 0;
  if (strlen(buf) > 0) parse_hotkey(buf, &hk);
  printf("Journal path [%s]: ", jpath ? jpath : expand(DEF_PATH)); fflush(stdout);
  char pbuf[512] = "";
  if (fgets(pbuf, sizeof(pbuf), stdin)) {
    pbuf[strcspn(pbuf, "\n")] = 0;
    if (strlen(pbuf) > 0) {
      char *e = expand(pbuf);
      if (jpath) free(jpath);
      jpath = strdup(e);
    }
  }
  jmkdir(); save_config();
  printf("\nSaved. Config: ~%s/%s\n", CONF_DIR, CONF_FILE);
  printf("Journal: %s\n", jpath);
  printf("Run 'qnote --listen' to start the daemon.\n");
}

// ── Interrupt handler for daemon mode ────────────────────────────────

static volatile int daemon_run = 1;
static void on_sig(int s) { (void)s; daemon_run = 0; }

// ── Main ─────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
  load_config();
  if (argc > 1) {
    if (!strcmp(argv[1], "--help")) { print_help(); return 0; }
    if (!strcmp(argv[1], "--all")) { list_all(argc > 2 ? atoi(argv[2]) : 0); return 0; }
    if (!strcmp(argv[1], "--today")) { show_today(); return 0; }
    if (!strcmp(argv[1], "--delete")) { delete_entry(); return 0; }
    if (!strcmp(argv[1], "--setup")) { setup(); return 0; }
    if (!strcmp(argv[1], "--search")) {
      if (argc < 3) { fprintf(stderr, "Usage: qnote --search <text>\n"); return 1; }
      search(argv[2]); return 0;
    }
    if (!strcmp(argv[1], "--listen")) {
      x11_init(); reg_hotkey(1);
      signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
      fprintf(stderr, "qnote listening (%s) ...\n", hk.str);
      while (daemon_run) {
        XEvent ev; XNextEvent(dpy, &ev);
        if (ev.type == KeyPress) {
          KeyCode kc = XKeysymToKeycode(dpy, hk.sym);
          if (kc && ev.xkey.keycode == kc) {
            unsigned int mod = ev.xkey.state & (Mod4Mask|ControlMask|Mod1Mask|ShiftMask);
            if (mod == hk.mod) run_note();
          }
        }
      }
      reg_hotkey(0); return 0;
    }
    fprintf(stderr, "Unknown option. Try 'qnote --help'.\n");
    return 1;
  }
  x11_init(); run_note();
  return 0;
}

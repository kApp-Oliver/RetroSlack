/*
 * RetroSlack — classic Mac UI. Slack Web API runs on the NSE host bridge;
 * this app only requests compact display payloads over RHTTP (http://rs/...).
 */

#include "../common/harness.h"
#include "../common/harness_guest.h"
#include "../common/mini_json.h"
#include "../common/rhttp.h"
#include "../common/strutil.h"

#include <Devices.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <ToolUtils.h>
#include <Windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum {
    kMaxChannels = 32,
    kMaxMessages = 12, /* only what we render; history lives on the bridge */
    kMaxReactions = 6,
    kNameLen = 48,
    kReactNameLen = 24,
    kTsLen = 24,
    kTextLen = 240,
    kWinContentW = 512,
    kWinContentH = 342,
    kHeaderH = 26,
    kChanPaneW = 140,
    kDividerX = 144,
    kMsgLeft = 152,
    kPaneHeaderH = 16,
    kMsgTop = 52, /* first message baseline */
    kMsgBottom = 250,
    kComposeTop = 268,
    kVisibleChannels = 13,
    kVisibleMessages = 14, /* display slots (name/body/react rows) */
    kLineH = 16,
    kChanRowH = 15,
    kRowName = 0,
    kRowBody = 1,
    kRowReact = 2,
    kAutoRefreshTicks = 30 * 60, /* ~30 seconds between quiet polls */
    kIdleBeforeRefreshTicks = 3 * 60,
    kUpArrowChar = 0x1E,
    kDownArrowChar = 0x1F,
    kEscapeChar = 0x1B,
    kEscapeKeyCode = 0x35,
    kClearKeyCode = 0x47,
    kUnixToMacEpoch = 2082844800UL
};

typedef struct Channel {
    char id[kNameLen];
    char name[kNameLen];
} Channel;

typedef struct Reaction {
    char name[kReactNameLen];
    int count;
    Boolean mine;
} Reaction;

typedef struct Message {
    char user_id[kNameLen];
    char user[kNameLen];
    char text[kTextLen];
    char ts_str[kTsLen];
    char thread_ts[kTsLen];
    unsigned long ts;
    Reaction reactions[kMaxReactions];
    int reaction_count;
    int reply_count;
} Message;

static WindowPtr gWin;
static Channel gChannels[kMaxChannels];
static int gChannelCount;
static int gSelectedChannel = -1;
static Message gMessages[kMaxMessages];
static int gMessageCount; /* messages currently held for display */
static int gMessageTotal; /* total in bridge cache */
static int gViewOff; /* bridge offset: 0 = newest */
static char gSelfId[kNameLen];
static char gSelfName[kNameLen];
static char gStatus[128];
static char gCompose[kTextLen];
static TEHandle gComposeTE;
static Rect gComposeRect;
static unsigned long gLastHistoryTicks;
static unsigned long gLastInputTicks;
static int gInNetwork;
static Boolean gHarnessQuit;
static Boolean gShowTimestamps;
static Boolean gInThread;
static char gThreadTs[kTsLen];
static Boolean gReactPrompt;
static int gReactTarget;
static int gRowMsg[kVisibleMessages];
static int gRowKind[kVisibleMessages]; /* kRowName / kRowBody / kRowReact */
static int gRowCount;

static void format_history_status(void);
static void draw_ui(void);

static unsigned long parse_slack_ts(const char *s) {
    unsigned long v = 0;
    if (!s || !s[0]) {
        return 0;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10UL + (unsigned long)(*s - '0');
        s++;
    }
    return v;
}

static void format_message_time(unsigned long unix_ts, char *out, int out_sz) {
    DateTimeRec dt;
    unsigned long mac;

    if (!out || out_sz < 12) {
        return;
    }
    if (!unix_ts) {
        strcpy(out, "--/-- --:--");
        return;
    }
    mac = unix_ts + kUnixToMacEpoch;
    SecondsToDate(mac, &dt);
    sprintf(out, "%02d/%02d %02d:%02d",
            (int)dt.month, (int)dt.day, (int)dt.hour, (int)dt.minute);
}

static void format_reactions_line(const Message *m, char *out, int out_sz) {
    int i;
    int n;

    if (!m || !out || out_sz < 8) {
        return;
    }
    n = 0;
    out[0] = '\0';
    for (i = 0; i < m->reaction_count; i++) {
        int left = out_sz - n - 1;
        int wrote;
        if (left < 10) {
            break;
        }
        wrote = sprintf(out + n, "%s:%s:%d",
                        i ? " " : "",
                        m->reactions[i].name,
                        m->reactions[i].count);
        if (wrote < 0 || wrote >= left) {
            out[n] = '\0';
            break;
        }
        n += wrote;
        if (m->reactions[i].mine && n + 1 < out_sz) {
            out[n++] = '*';
            out[n] = '\0';
        }
    }
}

/* Reaction chip hit-test; chips start at local_x origin of the reaction row. */
static int reaction_index_at_x(const Message *m, short local_x, short chip_left) {
    char piece[40];
    Str255 p;
    short x0;
    short x1;
    int i;

    if (!m || m->reaction_count <= 0) {
        return -1;
    }
    SetPort(gWin);
    TextFont(applFont);
    TextSize(9);
    TextFace(0);
    x0 = chip_left;
    for (i = 0; i < m->reaction_count; i++) {
        /* Keep labels short for Plus width */
        sprintf(piece, ":%.*s:%d%s",
                8, m->reactions[i].name,
                m->reactions[i].count,
                m->reactions[i].mine ? "*" : "");
        c_to_pstr(p, piece);
        x1 = (short)(x0 + StringWidth(p) + 8);
        if (local_x >= x0 && local_x < x1) {
            return i;
        }
        x0 = (short)(x1 + 4);
    }
    return -1;
}

static void build_visible_rows(void) {
    int msg = 0;
    int row = 0;

    gRowCount = 0;
    while (row < kVisibleMessages && msg < gMessageCount) {
        /* Username line */
        gRowMsg[row] = msg;
        gRowKind[row] = kRowName;
        row++;
        if (row >= kVisibleMessages) {
            break;
        }
        /* Body line */
        gRowMsg[row] = msg;
        gRowKind[row] = kRowBody;
        row++;
        if (gMessages[msg].reaction_count > 0 && row < kVisibleMessages) {
            gRowMsg[row] = msg;
            gRowKind[row] = kRowReact;
            row++;
        }
        msg++;
    }
    gRowCount = row;
}

static void set_status(const char *s) {
    strncpy(gStatus, s, sizeof(gStatus) - 1);
    gStatus[sizeof(gStatus) - 1] = '\0';
    if (gWin) {
        InvalRect(&gWin->portRect);
    }
}

/* Draw a 1-bit RetroSlack mark (hash + speech tail) with QuickDraw only.
 * Avoid CopyBits from const CODE data — odd baseAddr causes 68000 address errors. */
static void draw_logo(short left, short top) {
    Rect r;

    PenNormal();
    /* Vertical bars */
    SetRect(&r, left + 6, top + 0, left + 10, top + 12);
    PaintRect(&r);
    SetRect(&r, left + 14, top + 0, left + 18, top + 12);
    PaintRect(&r);
    /* Horizontal bars */
    SetRect(&r, left + 2, top + 2, left + 22, top + 5);
    PaintRect(&r);
    SetRect(&r, left + 2, top + 7, left + 22, top + 10);
    PaintRect(&r);
    /* Cut gaps where bars cross (white) */
    SetRect(&r, left + 6, top + 2, left + 10, top + 5);
    EraseRect(&r);
    SetRect(&r, left + 14, top + 2, left + 18, top + 5);
    EraseRect(&r);
    SetRect(&r, left + 6, top + 7, left + 10, top + 10);
    EraseRect(&r);
    SetRect(&r, left + 14, top + 7, left + 18, top + 10);
    EraseRect(&r);
    /* Re-paint bar segments for interlocking look */
    SetRect(&r, left + 6, top + 0, left + 10, top + 2);
    PaintRect(&r);
    SetRect(&r, left + 6, top + 5, left + 10, top + 7);
    PaintRect(&r);
    SetRect(&r, left + 6, top + 10, left + 10, top + 12);
    PaintRect(&r);
    SetRect(&r, left + 14, top + 0, left + 18, top + 2);
    PaintRect(&r);
    SetRect(&r, left + 14, top + 5, left + 18, top + 7);
    PaintRect(&r);
    SetRect(&r, left + 14, top + 10, left + 18, top + 12);
    PaintRect(&r);
    SetRect(&r, left + 2, top + 2, left + 6, top + 5);
    PaintRect(&r);
    SetRect(&r, left + 10, top + 2, left + 14, top + 5);
    PaintRect(&r);
    SetRect(&r, left + 18, top + 2, left + 22, top + 5);
    PaintRect(&r);
    SetRect(&r, left + 2, top + 7, left + 6, top + 10);
    PaintRect(&r);
    SetRect(&r, left + 10, top + 7, left + 14, top + 10);
    PaintRect(&r);
    SetRect(&r, left + 18, top + 7, left + 22, top + 10);
    PaintRect(&r);
    /* Speech-tail */
    MoveTo(left + 10, top + 12);
    LineTo(left + 8, top + 16);
    LineTo(left + 14, top + 12);
    LineTo(left + 10, top + 12);
}

static void draw_panel_frame(const Rect *r) {
    FrameRect(r);
}

/* ~12% dither — much lighter than qd.ltGray so black type stays readable. */
static Pattern gWashGray = {
    0x88, 0x00, 0x00, 0x00,
    0x22, 0x00, 0x00, 0x00
};

static void fill_heading(const Rect *r) {
    FillRect(r, &gWashGray);
}

static void draw_button(const Rect *r, const char *label, Boolean pressed) {
    Str255 p;
    short tw;

    if (pressed) {
        PaintRect(r);
        FrameRect(r);
    } else {
        EraseRect(r);
        FrameRect(r);
        MoveTo(r->right, r->top + 1);
        LineTo(r->right, r->bottom);
        LineTo(r->left + 1, r->bottom);
    }
    TextFont(systemFont);
    TextSize(9);
    TextFace(0);
    c_to_pstr(p, label);
    tw = StringWidth(p);
    MoveTo((short)((r->left + r->right - tw) / 2),
           (short)(r->top + 11));
    if (pressed) {
        TextMode(srcBic);
    }
    DrawString(p);
    TextMode(srcOr);
}

static void draw_scroll_arrow(short left, short top, Boolean up, Boolean enabled) {
    Rect r;
    SetRect(&r, left, top, left + 14, top + 12);
    EraseRect(&r);
    FrameRect(&r);
    if (!enabled) {
        return;
    }
    PenSize(1, 1);
    if (up) {
        MoveTo(left + 7, top + 3);
        LineTo(left + 3, top + 8);
        LineTo(left + 11, top + 8);
        LineTo(left + 7, top + 3);
    } else {
        MoveTo(left + 7, top + 9);
        LineTo(left + 3, top + 4);
        LineTo(left + 11, top + 4);
        LineTo(left + 7, top + 9);
    }
}

/* Light reaction pills — outline only; mine gets gray fill (not solid black). */
static void draw_reaction_chips(const Message *m, short left, short top) {
    char piece[40];
    Str255 p;
    Rect chip;
    short x = left;
    int i;

    TextFont(applFont);
    TextSize(9);
    TextFace(0);
    for (i = 0; i < m->reaction_count; i++) {
        short tw;
        sprintf(piece, " :%.*s: %d%s ",
                8, m->reactions[i].name,
                m->reactions[i].count,
                m->reactions[i].mine ? "*" : "");
        c_to_pstr(p, piece);
        tw = StringWidth(p);
        SetRect(&chip, x, top - 9, x + tw + 4, top + 3);
        if (m->reactions[i].mine) {
            FillRect(&chip, &qd.ltGray);
        } else {
            EraseRect(&chip);
        }
        FrameRect(&chip);
        MoveTo(x + 2, top);
        DrawString(p);
        x = (short)(chip.right + 4);
    }
}

static void draw_ui(void) {
    Rect r;
    Rect chanPane;
    Rect msgPane;
    Rect header;
    Rect footer;
    Rect btn;
    int i;
    int maxChan;
    Str255 p;
    char title[80];
    Boolean canOlder;
    Boolean canNewer;

    SetPort(gWin);
    EraseRect(&gWin->portRect);
    PenNormal();
    TextFont(systemFont);
    TextSize(9);
    TextFace(0);

    /* --- Title bar strip --- */
    SetRect(&header, 0, 0, kWinContentW, kHeaderH);
    FillRect(&header, &gWashGray);
    MoveTo(0, kHeaderH - 1);
    LineTo(kWinContentW, kHeaderH - 1);
    draw_logo(6, 5);
    TextFont(systemFont);
    TextFace(bold);
    TextSize(12);
    MoveTo(36, 17);
    DrawString("\pRetroSlack");
    TextFace(0);
    TextFont(applFont);
    TextSize(9);
    {
        char shortStatus[64];
        short tw;
        short sx;
        strncpy(shortStatus, gStatus, 48);
        shortStatus[48] = '\0';
        c_to_pstr(p, shortStatus);
        tw = StringWidth(p);
        sx = (short)(kWinContentW - 10 - tw);
        if (sx < 170) {
            sx = 170;
        }
        MoveTo(sx, 17);
        DrawString(p);
    }

    /* --- Channel pane --- */
    SetRect(&chanPane, 4, kHeaderH + 4, kDividerX - 2, kMsgBottom);
    draw_panel_frame(&chanPane);
    SetRect(&r, chanPane.left + 1, chanPane.top + 1,
            chanPane.right - 1, chanPane.top + 1 + kPaneHeaderH);
    fill_heading(&r);
    MoveTo(r.left, r.bottom);
    LineTo(r.right, r.bottom);
    TextFont(systemFont);
    TextFace(bold);
    TextSize(9);
    MoveTo(r.left + 6, r.top + 12);
    DrawString("\pChannels");
    TextFace(0);

    maxChan = gChannelCount;
    if (maxChan > kVisibleChannels) {
        maxChan = kVisibleChannels;
    }
    TextFont(applFont);
    TextSize(10);
    for (i = 0; i < maxChan; i++) {
        Rect row;
        char line[64];
        short rowTop = (short)(r.bottom + 3 + i * kChanRowH);
        SetRect(&row, chanPane.left + 2, rowTop,
                chanPane.right - 2, rowTop + kChanRowH - 1);
        sprintf(line, "#%s", gChannels[i].name);
        c_to_pstr(p, line);
        if (i == gSelectedChannel) {
            PaintRect(&row);
            TextMode(srcBic);
            MoveTo(row.left + 5, row.top + 11);
            DrawString(p);
            TextMode(srcOr);
        } else {
            MoveTo(row.left + 5, row.top + 11);
            DrawString(p);
        }
    }

    /* --- Message pane --- */
    SetRect(&msgPane, kMsgLeft - 4, kHeaderH + 4, kWinContentW - 4, kMsgBottom);
    draw_panel_frame(&msgPane);
    SetRect(&r, msgPane.left + 1, msgPane.top + 1,
            msgPane.right - 1, msgPane.top + 1 + kPaneHeaderH);
    fill_heading(&r);
    MoveTo(r.left, r.bottom);
    LineTo(r.right, r.bottom);

    if (gSelectedChannel >= 0 && gSelectedChannel < gChannelCount) {
        if (gInThread) {
            sprintf(title, "#%s - thread", gChannels[gSelectedChannel].name);
        } else {
            sprintf(title, "#%s", gChannels[gSelectedChannel].name);
        }
    } else {
        strcpy(title, "Messages");
    }
    TextFont(systemFont);
    TextFace(bold);
    TextSize(9);
    c_to_pstr(p, title);
    MoveTo(r.left + 6, r.top + 12);
    DrawString(p);
    TextFace(0);

    /* Thread back button */
    if (gInThread && !gReactPrompt) {
        SetRect(&btn, r.right - 74, r.top + 1, r.right - 38, r.bottom - 1);
        draw_button(&btn, "Back", false);
    }

    canNewer = (gViewOff > 0);
    canOlder = (gMessageTotal > gViewOff + gMessageCount);

    SetRect(&btn, r.right - 34, r.top + 1, r.right - 19, r.bottom - 1);
    draw_scroll_arrow(btn.left, btn.top, true, canNewer);
    SetRect(&btn, r.right - 17, r.top + 1, r.right - 2, r.bottom - 1);
    draw_scroll_arrow(btn.left, btn.top, false, canOlder);

    build_visible_rows();
    for (i = 0; i < gRowCount; i++) {
        int idx = gRowMsg[i];
        int kind = gRowKind[i];
        short rowTop = (short)(kMsgTop + i * kLineH);
        Rect row;
        Boolean selected = (gInThread && idx == gReactTarget);

        SetRect(&row, msgPane.left + 3, rowTop - 12,
                msgPane.right - 3, rowTop + 3);

        if (kind == kRowReact) {
            draw_reaction_chips(&gMessages[idx], (short)(msgPane.left + 22), rowTop);
            continue;
        }

        if (selected && kind == kRowName) {
            /* Selection marker: left bar, not full invert */
            Rect bar;
            SetRect(&bar, msgPane.left + 3, rowTop - 11,
                    msgPane.left + 6, rowTop + kLineH + 2);
            PaintRect(&bar);
        }

        if (kind == kRowName) {
            char when[16];
            char line[80];
            TextFont(applFont);
            TextSize(10);
            TextFace(bold);
            sprintf(line, "%.14s", gMessages[idx].user);
            c_to_pstr(p, line);
            MoveTo(msgPane.left + 12, rowTop);
            DrawString(p);
            TextFace(0);
            if (gShowTimestamps) {
                format_message_time(gMessages[idx].ts, when, (int)sizeof(when));
                TextSize(9);
                c_to_pstr(p, when);
                MoveTo((short)(msgPane.right - 8 - StringWidth(p)), rowTop);
                DrawString(p);
            }
            if (gMessages[idx].reply_count > 0 && !gInThread) {
                char badge[24];
                TextSize(9);
                sprintf(badge, "%d repl%s", gMessages[idx].reply_count,
                        gMessages[idx].reply_count == 1 ? "y" : "ies");
                c_to_pstr(p, badge);
                MoveTo(msgPane.left + 120, rowTop);
                DrawString(p);
            }
        } else { /* kRowBody */
            char line[96];
            TextFont(applFont);
            TextSize(10);
            TextFace(0);
            sprintf(line, "%.52s", gMessages[idx].text);
            c_to_pstr(p, line);
            MoveTo(msgPane.left + 12, rowTop);
            DrawString(p);
        }
    }

    /* --- Compose footer --- */
    SetRect(&footer, 4, kMsgBottom + 4, kWinContentW - 4, kWinContentH - 4);
    draw_panel_frame(&footer);
    SetRect(&r, footer.left + 1, footer.top + 1, footer.right - 1, footer.top + 15);
    fill_heading(&r);
    MoveTo(r.left, r.bottom);
    LineTo(r.right, r.bottom);
    TextFont(systemFont);
    TextFace(bold);
    TextSize(9);
    MoveTo(r.left + 6, r.top + 11);
    if (gReactPrompt) {
        DrawString("\pAdd reaction");
    } else if (gInThread) {
        DrawString("\pReply");
    } else {
        DrawString("\pMessage");
    }
    TextFont(applFont);
    TextFace(bold);
    MoveTo(r.left + 88, r.top + 11);
    if (gReactPrompt) {
        DrawString("\pemoji name · Return adds · Esc cancels");
    } else if (gInThread) {
        DrawString("\pReturn sends · Cmd-E reacts · Esc back");
    } else {
        DrawString("\pReturn sends · Cmd-C channels · [ ] scrolls");
    }
    TextFace(0);

    /* Compose field — single clean frame */
    r = gComposeRect;
    EraseRect(&r);
    FrameRect(&r);
    if (gComposeTE) {
        TEUpdate(&gComposeRect, gComposeTE);
    }
}

static void note_user_input(void) {
    gLastInputTicks = TickCount();
}

static void on_channel(const char *obj, int obj_len, void *userdata) {
    char tmp[512];
    char id[kNameLen];
    char name[kNameLen];
    (void)userdata;
    if (gChannelCount >= kMaxChannels) {
        return;
    }
    if (obj_len >= (int)sizeof(tmp)) {
        obj_len = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, obj, (size_t)obj_len);
    tmp[obj_len] = '\0';
    if (!json_find_string(tmp, "id", id, sizeof(id))) {
        return;
    }
    if (!json_find_string(tmp, "name", name, sizeof(name))) {
        return;
    }
    strncpy(gChannels[gChannelCount].id, id, kNameLen - 1);
    strncpy(gChannels[gChannelCount].name, name, kNameLen - 1);
    gChannels[gChannelCount].id[kNameLen - 1] = '\0';
    gChannels[gChannelCount].name[kNameLen - 1] = '\0';
    gChannelCount++;
}

static void on_reaction_disp(const char *obj, int obj_len, void *userdata) {
    Message *m = (Message *)userdata;
    char tmp[128];
    char name[kReactNameLen];
    char countbuf[16];
    char minebuf[8];
    int count = 0;

    if (!m || m->reaction_count >= kMaxReactions) {
        return;
    }
    if (obj_len >= (int)sizeof(tmp)) {
        obj_len = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, obj, (size_t)obj_len);
    tmp[obj_len] = '\0';
    if (!json_find_string(tmp, "n", name, sizeof(name)) || !name[0]) {
        return;
    }
    if (json_find_raw(tmp, "c", countbuf, sizeof(countbuf))) {
        count = atoi(countbuf);
    }
    if (count <= 0) {
        count = 1;
    }
    strncpy(m->reactions[m->reaction_count].name, name, kReactNameLen - 1);
    m->reactions[m->reaction_count].name[kReactNameLen - 1] = '\0';
    m->reactions[m->reaction_count].count = count;
    m->reactions[m->reaction_count].mine = false;
    if (json_find_raw(tmp, "m", minebuf, sizeof(minebuf))) {
        m->reactions[m->reaction_count].mine =
            (minebuf[0] == '1' || strcmp(minebuf, "true") == 0);
    }
    m->reaction_count++;
}

static void parse_display_message(const char *obj, int obj_len, Message *m) {
    char tmp[1024];
    char nbuf[24];
    char rcbuf[16];

    memset(m, 0, sizeof(*m));
    if (obj_len >= (int)sizeof(tmp)) {
        obj_len = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, obj, (size_t)obj_len);
    tmp[obj_len] = '\0';

    json_find_string(tmp, "u", m->user, kNameLen);
    json_find_string(tmp, "uid", m->user_id, kNameLen);
    json_find_string(tmp, "t", m->text, kTextLen);
    json_find_string(tmp, "ts", m->ts_str, kTsLen);
    json_find_string(tmp, "tt", m->thread_ts, kTsLen);
    if (json_find_raw(tmp, "n", nbuf, sizeof(nbuf))) {
        m->ts = (unsigned long)atol(nbuf);
    } else if (m->ts_str[0]) {
        m->ts = parse_slack_ts(m->ts_str);
    }
    if (json_find_raw(tmp, "rc", rcbuf, sizeof(rcbuf))) {
        m->reply_count = atoi(rcbuf);
    }
    if (!m->user[0]) {
        strcpy(m->user, m->user_id[0] ? m->user_id : "?");
    }
    json_foreach_object_in_array(tmp, "r", on_reaction_disp, m);
}

static void on_view_message(const char *obj, int obj_len, void *userdata) {
    (void)userdata;
    if (gMessageCount >= kMaxMessages) {
        return;
    }
    parse_display_message(obj, obj_len, &gMessages[gMessageCount]);
    gMessageCount++;
}

/* Extract balanced {...} starting at '{'. Returns length including braces. */
static int extract_object(const char *p, char *out, int out_sz) {
    int depth = 0;
    int i = 0;
    if (!p || *p != '{' || !out || out_sz < 3) {
        return 0;
    }
    do {
        if (i >= out_sz - 1) {
            return 0;
        }
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
        } else if (*p == '"') {
            out[i++] = *p++;
            while (*p && *p != '"' && i < out_sz - 1) {
                if (*p == '\\' && p[1]) {
                    out[i++] = *p++;
                    if (i < out_sz - 1) {
                        out[i++] = *p++;
                    }
                    continue;
                }
                out[i++] = *p++;
            }
            if (*p == '"' && i < out_sz - 1) {
                out[i++] = *p++;
            }
            continue;
        }
        out[i++] = *p++;
    } while (*p && depth > 0);
    out[i] = '\0';
    return i;
}

static OSErr rs_request(const char *method, const char *path_and_query,
                        const char *json_body, Handle *out) {
    char url[256];
    short status = 0;
    OSErr err;
    const char *hdr;

    sprintf(url, "http://rs%s", path_and_query);
    if (out) {
        *out = NULL;
    }
    if (json_body) {
        hdr = "Header: Content-Type: application/json; charset=utf-8\n";
        gInNetwork = 1;
        err = RHTTPRequest(method, url, hdr, json_body, (long)strlen(json_body),
                           &status, out);
        gInNetwork = 0;
    } else {
        hdr = "Header: Content-Type: application/json\n";
        gInNetwork = 1;
        err = RHTTPRequest(method, url, hdr, NULL, 0, &status, out);
        gInNetwork = 0;
    }
    if (err != noErr) {
        return err;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        if (out && *out) {
            DisposeHandle(*out);
            *out = NULL;
        }
        return ioErr;
    }
    return noErr;
}

static Boolean body_ok(Handle body) {
    char ok[16];
    if (!body || GetHandleSize(body) <= 0) {
        return false;
    }
    HLock(body);
    if (json_find_raw((char *)*body, "ok", ok, sizeof(ok)) && strcmp(ok, "true") == 0) {
        HUnlock(body);
        return true;
    }
    HUnlock(body);
    return false;
}

static void apply_view_meta(const char *json) {
    char buf[24];
    char th[8];
    if (json_find_raw(json, "total", buf, sizeof(buf))) {
        gMessageTotal = atoi(buf);
    }
    if (json_find_raw(json, "off", buf, sizeof(buf))) {
        gViewOff = atoi(buf);
    }
    if (json_find_raw(json, "th", th, sizeof(th))) {
        gInThread = (th[0] == '1');
    }
}

static void apply_view_body(Handle body) {
    gMessageCount = 0;
    if (!body) {
        return;
    }
    HLock(body);
    apply_view_meta((char *)*body);
    json_foreach_object_in_array((char *)*body, "msgs", on_view_message, NULL);
    HUnlock(body);
}

static int messages_same(const Message *a, int na, const Message *b, int nb) {
    int i;
    int r;
    if (na != nb) {
        return 0;
    }
    for (i = 0; i < na; i++) {
        if (strcmp(a[i].user_id, b[i].user_id) != 0
            || strcmp(a[i].text, b[i].text) != 0
            || a[i].reaction_count != b[i].reaction_count
            || a[i].reply_count != b[i].reply_count) {
            return 0;
        }
        for (r = 0; r < a[i].reaction_count; r++) {
            if (a[i].reactions[r].count != b[i].reactions[r].count
                || a[i].reactions[r].mine != b[i].reactions[r].mine
                || strcmp(a[i].reactions[r].name, b[i].reactions[r].name) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

static void format_history_status(void) {
    if (gSelectedChannel < 0 || gSelectedChannel >= gChannelCount) {
        return;
    }
    if (gInThread) {
        sprintf(gStatus, "thread · %d msgs", gMessageTotal);
    } else if (gViewOff > 0) {
        sprintf(gStatus, "%d msgs · +%d", gMessageTotal, gViewOff);
    } else {
        sprintf(gStatus, "%d msgs", gMessageTotal);
    }
}

static OSErr fetch_view(Boolean show_status) {
    Handle body = NULL;
    OSErr err;
    char path[64];

    if (gSelectedChannel < 0 || gSelectedChannel >= gChannelCount) {
        if (show_status) {
            set_status("Select a channel first");
        }
        return paramErr;
    }
    if (show_status) {
        set_status(gInThread ? "Loading thread..." : "Loading...");
        draw_ui();
    }
    sprintf(path, "/view?off=%d&n=%d", gViewOff, kMaxMessages);
    err = rs_request("GET", path, NULL, &body);
    if (err != noErr || !body_ok(body)) {
        if (body) {
            DisposeHandle(body);
        }
        if (show_status) {
            set_status(gInThread ? "thread failed" : "view failed");
        }
        return err != noErr ? err : ioErr;
    }
    apply_view_body(body);
    DisposeHandle(body);
    gLastHistoryTicks = TickCount();
    format_history_status();
    if (show_status) {
        set_status(gStatus);
    }
    return noErr;
}

static OSErr open_view(Boolean thread, const char *thread_ts, Boolean show_status) {
    Handle body = NULL;
    OSErr err;
    char json[160];

    if (gSelectedChannel < 0 || gSelectedChannel >= gChannelCount) {
        if (show_status) {
            set_status("Select a channel first");
        }
        return paramErr;
    }
    if (thread && thread_ts && thread_ts[0]) {
        sprintf(json, "{\"channel\":\"%s\",\"thread\":\"%s\"}",
                gChannels[gSelectedChannel].id, thread_ts);
        gInThread = true;
        strncpy(gThreadTs, thread_ts, kTsLen - 1);
        gThreadTs[kTsLen - 1] = '\0';
    } else {
        sprintf(json, "{\"channel\":\"%s\"}", gChannels[gSelectedChannel].id);
        gInThread = false;
        gThreadTs[0] = '\0';
    }
    gViewOff = 0;
    gReactTarget = 0;
    if (show_status) {
        set_status(gInThread ? "Loading thread..." : "Loading history...");
        draw_ui();
    }
    err = rs_request("POST", "/open", json, &body);
    if (err != noErr || !body_ok(body)) {
        if (body) {
            DisposeHandle(body);
        }
        if (show_status) {
            set_status(gInThread ? "thread failed" : "open failed");
        }
        return err != noErr ? err : ioErr;
    }
    apply_view_body(body);
    DisposeHandle(body);
    gLastHistoryTicks = TickCount();
    format_history_status();
    if (show_status) {
        set_status(gStatus);
    }
    return noErr;
}

/* delta > 0 = older; delta < 0 = newer. Asks bridge for the one new edge message. */
static Boolean scroll_messages(int delta) {
    Handle body = NULL;
    char path[48];
    char moved[8];
    char edge[8];
    char msgobj[1024];
    const char *p;
    Message incoming;
    OSErr err;
    int i;

    if (delta == 0) {
        return false;
    }
    sprintf(path, "/nav?dir=%s&n=%d", delta > 0 ? "older" : "newer", kMaxMessages);
    err = rs_request("GET", path, NULL, &body);
    if (err != noErr || !body_ok(body)) {
        if (body) {
            DisposeHandle(body);
        }
        return false;
    }
    HLock(body);
    if (!json_find_raw((char *)*body, "moved", moved, sizeof(moved))
        || moved[0] != '1') {
        HUnlock(body);
        DisposeHandle(body);
        return false;
    }
    apply_view_meta((char *)*body);
    p = strstr((char *)*body, "\"msg\"");
    if (!p) {
        HUnlock(body);
        DisposeHandle(body);
        format_history_status();
        return true;
    }
    while (*p && *p != '{') {
        p++;
    }
    if (!extract_object(p, msgobj, (int)sizeof(msgobj))) {
        HUnlock(body);
        DisposeHandle(body);
        format_history_status();
        return true;
    }
    json_find_raw((char *)*body, "edge", edge, sizeof(edge));
    parse_display_message(msgobj, (int)strlen(msgobj), &incoming);
    HUnlock(body);
    DisposeHandle(body);

    if (edge[0] == '1') {
        /* older: drop newest (index 0), append at end */
        if (gMessageCount >= kMaxMessages) {
            for (i = 0; i < gMessageCount - 1; i++) {
                gMessages[i] = gMessages[i + 1];
            }
            gMessages[gMessageCount - 1] = incoming;
        } else {
            gMessages[gMessageCount++] = incoming;
        }
    } else {
        /* newer: drop oldest, prepend */
        if (gMessageCount >= kMaxMessages) {
            for (i = gMessageCount - 1; i > 0; i--) {
                gMessages[i] = gMessages[i - 1];
            }
            gMessages[0] = incoming;
        } else {
            for (i = gMessageCount; i > 0; i--) {
                gMessages[i] = gMessages[i - 1];
            }
            gMessages[0] = incoming;
            gMessageCount++;
        }
    }
    format_history_status();
    return true;
}

static void normalize_emoji_name(char *name) {
    char *s = name;
    char *d = name;
    int len;

    if (!name) {
        return;
    }
    while (*s == ':' || *s == ' ' || *s == '\t') {
        s++;
    }
    while (*s) {
        *d++ = *s++;
    }
    *d = '\0';
    len = (int)strlen(name);
    while (len > 0 && (name[len - 1] == ':' || name[len - 1] == ' ' || name[len - 1] == '\t')) {
        name[--len] = '\0';
    }
}

static void react_set(int msg_idx, const char *raw_name, Boolean force_add) {
    Message *m;
    char name[kReactNameLen];
    char json[192];
    Handle body = NULL;
    Boolean want_add;
    Boolean had_mine;
    int i;
    OSErr err;

    if (msg_idx < 0 || msg_idx >= gMessageCount) {
        return;
    }
    m = &gMessages[msg_idx];
    if (!m->ts_str[0]) {
        set_status("Message has no timestamp");
        return;
    }
    strncpy(name, raw_name, kReactNameLen - 1);
    name[kReactNameLen - 1] = '\0';
    normalize_emoji_name(name);
    if (!name[0]) {
        set_status("Empty emoji name");
        return;
    }

    had_mine = false;
    for (i = 0; i < m->reaction_count; i++) {
        if (strcmp(m->reactions[i].name, name) == 0) {
            had_mine = m->reactions[i].mine;
            break;
        }
    }
    want_add = force_add ? true : !had_mine;

    sprintf(gStatus, "%s :%s:...", want_add ? "Adding" : "Removing", name);
    draw_ui();

    sprintf(json, "{\"ts\":\"%s\",\"name\":\"%s\",\"add\":%s}",
            m->ts_str, name, want_add ? "true" : "false");
    err = rs_request("POST", "/react", json, &body);
    if (err != noErr || !body_ok(body)) {
        if (body) {
            DisposeHandle(body);
        }
        set_status("react failed");
        draw_ui();
        return;
    }
    apply_view_body(body);
    DisposeHandle(body);
    format_history_status();
    draw_ui();
}

static void begin_react_prompt(void) {
    if (!gInThread) {
        set_status("Open a thread first, then Cmd-E");
        return;
    }
    if (gReactTarget < 0 || gReactTarget >= gMessageCount) {
        gReactTarget = 0;
    }
    gReactPrompt = true;
    if (gComposeTE) {
        TESetText("", 0, gComposeTE);
        TEActivate(gComposeTE);
    }
    sprintf(gStatus, "React to msg %d — type emoji name", gReactTarget + 1);
    draw_ui();
}

static void cancel_react_prompt(void) {
    gReactPrompt = false;
    if (gComposeTE) {
        TESetText("", 0, gComposeTE);
    }
    format_history_status();
    draw_ui();
}

static void submit_react_prompt(void) {
    char name[kReactNameLen];
    int len = 0;

    if (!gReactPrompt) {
        return;
    }
    if (gComposeTE) {
        CharsHandle ch = (CharsHandle)(*gComposeTE)->hText;
        len = (*gComposeTE)->teLength;
        if (len >= kReactNameLen) {
            len = kReactNameLen - 1;
        }
        HLock((Handle)ch);
        memcpy(name, *ch, (size_t)len);
        HUnlock((Handle)ch);
    }
    name[len] = '\0';
    gReactPrompt = false;
    if (gComposeTE) {
        TESetText("", 0, gComposeTE);
    }
    react_set(gReactTarget, name, true);
}

static void load_self(void) {
    Handle body = NULL;
    OSErr err;

    gSelfId[0] = '\0';
    strcpy(gSelfName, "me");

    err = rs_request("GET", "/auth", NULL, &body);
    if (err != noErr || !body_ok(body)) {
        if (body) {
            DisposeHandle(body);
        }
        return;
    }
    HLock(body);
    json_find_string((char *)*body, "uid", gSelfId, sizeof(gSelfId));
    json_find_string((char *)*body, "user", gSelfName, sizeof(gSelfName));
    HUnlock(body);
    DisposeHandle(body);
}

static void exit_thread(void) {
    gReactPrompt = false;
    gInThread = false;
    gThreadTs[0] = '\0';
    gReactTarget = 0;
    open_view(false, NULL, true);
}

static void open_thread_at(int msg_idx) {
    char root[kTsLen];

    if (msg_idx < 0 || msg_idx >= gMessageCount) {
        return;
    }
    if (gMessages[msg_idx].thread_ts[0]
        && strcmp(gMessages[msg_idx].thread_ts, gMessages[msg_idx].ts_str) != 0) {
        strncpy(root, gMessages[msg_idx].thread_ts, kTsLen - 1);
    } else {
        strncpy(root, gMessages[msg_idx].ts_str, kTsLen - 1);
    }
    root[kTsLen - 1] = '\0';
    if (!root[0]) {
        set_status("Message has no timestamp");
        return;
    }
    gReactPrompt = false;
    open_view(true, root, true);
}

static void maybe_auto_refresh(void) {
    Message snapshot[kMaxMessages];
    int snapCount;
    unsigned long now;
    EventRecord peek;

    if (gInNetwork) {
        return;
    }
    if (gSelectedChannel < 0 || gSelectedChannel >= gChannelCount) {
        return;
    }
    if (gComposeTE && (*gComposeTE)->teLength > 0) {
        return;
    }
    if (gReactPrompt) {
        return;
    }
    if (EventAvail(everyEvent, &peek)) {
        return;
    }

    now = TickCount();
    if (gLastInputTicks != 0
        && (now - gLastInputTicks) < (unsigned long)kIdleBeforeRefreshTicks) {
        return;
    }
    if (gLastHistoryTicks != 0
        && (now - gLastHistoryTicks) < (unsigned long)kAutoRefreshTicks) {
        return;
    }

    snapCount = gMessageCount;
    memcpy(snapshot, gMessages, (size_t)snapCount * sizeof(Message));

    if (fetch_view(false) != noErr) {
        memcpy(gMessages, snapshot, (size_t)snapCount * sizeof(Message));
        gMessageCount = snapCount;
        gLastHistoryTicks = TickCount();
        return;
    }
    if (!messages_same(snapshot, snapCount, gMessages, gMessageCount)) {
        draw_ui();
    }
}

static void load_channels(void) {
    Handle body = NULL;
    OSErr err;

    gChannelCount = 0;
    set_status("Loading channels...");
    draw_ui();

    err = rs_request("GET", "/channels", NULL, &body);
    if (err != noErr || !body_ok(body)) {
        if (body) {
            DisposeHandle(body);
        }
        sprintf(gStatus, "channels failed (err=%d)", (int)err);
        set_status(gStatus);
        return;
    }
    HLock(body);
    json_foreach_object_in_array((char *)*body, "channels", on_channel, NULL);
    HUnlock(body);
    DisposeHandle(body);
    if (gChannelCount > 0 && gSelectedChannel < 0) {
        gSelectedChannel = 0;
    }
    sprintf(gStatus, "%d channels", gChannelCount);
    set_status(gStatus);
}

static void load_history(void) {
    gReactPrompt = false;
    open_view(false, NULL, true);
}

static void post_message(void) {
    Handle body = NULL;
    char json[512];
    char text[kTextLen];
    OSErr err;
    int i;

    if (gSelectedChannel < 0) {
        set_status("No channel selected");
        return;
    }
    if (gComposeTE) {
        CharsHandle ch = (CharsHandle)(*gComposeTE)->hText;
        int len = (*gComposeTE)->teLength;
        if (len >= kTextLen) {
            len = kTextLen - 1;
        }
        HLock((Handle)ch);
        memcpy(text, *ch, (size_t)len);
        HUnlock((Handle)ch);
        text[len] = '\0';
    } else {
        strcpy(text, gCompose);
    }
    if (!text[0]) {
        set_status("Empty message");
        return;
    }

    {
        char esc[kTextLen * 2];
        int j = 0;
        for (i = 0; text[i] && j < (int)sizeof(esc) - 2; i++) {
            if (text[i] == '"' || text[i] == '\\') {
                esc[j++] = '\\';
            }
            if (text[i] == '\n') {
                esc[j++] = '\\';
                esc[j++] = 'n';
            } else {
                esc[j++] = text[i];
            }
        }
        esc[j] = '\0';
        sprintf(json, "{\"text\":\"%s\"}", esc);
    }

    if (gComposeTE) {
        TESetText("", 0, gComposeTE);
        TEActivate(gComposeTE);
    }
    sprintf(gStatus, "#%s%s - sending...",
            gChannels[gSelectedChannel].name,
            gInThread ? " thread" : "");
    draw_ui();

    err = rs_request("POST", "/post", json, &body);
    if (err != noErr || !body_ok(body)) {
        if (body) {
            DisposeHandle(body);
        }
        set_status("post failed");
        draw_ui();
        return;
    }
    apply_view_body(body);
    DisposeHandle(body);
    format_history_status();
    draw_ui();
    if (gComposeTE) {
        TEActivate(gComposeTE);
    }
}

static void do_click(Point where) {
    int i;
    int maxChan;
    Rect chanPane;
    Rect msgPane;
    Rect hdr;
    Rect btn;
    short listTop;

    note_user_input();
    GlobalToLocal(&where);

    SetRect(&chanPane, 4, kHeaderH + 4, kDividerX - 2, kMsgBottom);
    SetRect(&msgPane, kMsgLeft - 4, kHeaderH + 4, kWinContentW - 4, kMsgBottom);
    SetRect(&hdr, msgPane.left + 2, msgPane.top + 2,
            msgPane.right - 2, msgPane.top + 2 + kPaneHeaderH);

    /* Channel list */
    listTop = (short)(chanPane.top + 2 + kPaneHeaderH + 2);
    maxChan = gChannelCount;
    if (maxChan > kVisibleChannels) {
        maxChan = kVisibleChannels;
    }
    for (i = 0; i < maxChan; i++) {
        Rect row;
        SetRect(&row, chanPane.left + 3, listTop + i * kChanRowH,
                chanPane.right - 3, listTop + (i + 1) * kChanRowH - 1);
        if (PtInRect(where, &row)) {
            gSelectedChannel = i;
            load_history();
            draw_ui();
            return;
        }
    }

    /* Message header controls */
    if (gInThread && !gReactPrompt) {
        SetRect(&btn, hdr.right - 74, hdr.top + 1, hdr.right - 38, hdr.bottom - 1);
        if (PtInRect(where, &btn)) {
            exit_thread();
            draw_ui();
            return;
        }
    }
    SetRect(&btn, hdr.right - 34, hdr.top + 1, hdr.right - 19, hdr.bottom - 1);
    if (PtInRect(where, &btn)) {
        if (scroll_messages(-1)) {
            draw_ui();
        }
        return;
    }
    SetRect(&btn, hdr.right - 17, hdr.top + 1, hdr.right - 2, hdr.bottom - 1);
    if (PtInRect(where, &btn)) {
        if (scroll_messages(1)) {
            draw_ui();
        }
        return;
    }

    build_visible_rows();
    for (i = 0; i < gRowCount; i++) {
        Rect row;
        short rowTop = (short)(kMsgTop + i * kLineH);
        int kind = gRowKind[i];
        SetRect(&row, msgPane.left + 3, rowTop - 12,
                msgPane.right - 3, rowTop + 3);
        if (!PtInRect(where, &row)) {
            continue;
        }
        if (kind == kRowReact) {
            int ri = reaction_index_at_x(&gMessages[gRowMsg[i]], where.h,
                                         (short)(msgPane.left + 22));
            if (ri >= 0) {
                react_set(gRowMsg[i], gMessages[gRowMsg[i]].reactions[ri].name, false);
            }
            return;
        }
        if (gInThread) {
            gReactTarget = gRowMsg[i];
            draw_ui();
            return;
        }
        open_thread_at(gRowMsg[i]);
        draw_ui();
        return;
    }
    if (gComposeTE && PtInRect(where, &gComposeRect)) {
        TEClick(where, false, gComposeTE);
    }
}

static int slack_poll_stop(void) {
    if (gInNetwork) {
        return 0;
    }
    return RHTTPPollStop();
}

static void rhttp_yield_ui(void) {
    EventRecord ev;

    if (gComposeTE) {
        TEIdle(gComposeTE);
    }
    while (EventAvail(everyEvent, &ev)) {
        if (ev.what == updateEvt) {
            if (!GetNextEvent(updateMask, &ev)) {
                break;
            }
            if ((WindowPtr)ev.message == gWin) {
                BeginUpdate(gWin);
                draw_ui();
                EndUpdate(gWin);
            }
        } else if (ev.what == keyDown || ev.what == autoKey) {
            char c;
            Boolean cmd;
            if (!GetNextEvent(keyDownMask | autoKeyMask, &ev)) {
                break;
            }
            c = (char)(ev.message & charCodeMask);
            cmd = (ev.modifiers & cmdKey) != 0;
            if (!cmd && c != '\r' && c != '\n' && c != kEscapeChar && gComposeTE) {
                TEKey(c, gComposeTE);
                SetPort(gWin);
                EraseRect(&gComposeRect);
                FrameRect(&gComposeRect);
                {
                    Rect inset = gComposeRect;
                    InsetRect(&inset, 1, 1);
                    FrameRect(&inset);
                }
                TEUpdate(&gComposeRect, gComposeTE);
            }
        } else if (ev.what == nullEvent) {
            break;
        } else {
            break;
        }
    }
}

static void init_app(void) {
    Rect bounds;
    OSErr err;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    HarnessInstallQuitAE(&gHarnessQuit);
    RHTTPSetYieldProc(rhttp_yield_ui);

    SetRect(&bounds, 40, 40, 40 + kWinContentW, 40 + kWinContentH);
    gWin = NewWindow(NULL, &bounds, "\pRetroSlack", true, documentProc,
                     (WindowPtr)-1, true, 0);
    SetPort(gWin);

    /* Compose inset inside footer panel */
    SetRect(&gComposeRect, 10, kComposeTop, kWinContentW - 10, kWinContentH - 10);
    gComposeTE = TENew(&gComposeRect, &gComposeRect);
    TEActivate(gComposeTE);

    strcpy(gStatus, "Opening RHTTP...");
    draw_ui();

    err = RHTTPOpen();
    if (err != noErr) {
        set_status("RHTTP open failed - start NSE / Arduino");
    } else {
        HarnessGuestSetPoll(slack_poll_stop);
        load_self();
        set_status("Connected - Cmd-C/R/T/E, click react");
    }
    draw_ui();
}

static Boolean is_back_key(EventRecord *ev, char c, Boolean cmd) {
    short key = (short)((ev->message & keyCodeMask) >> 8);

    if (c == kEscapeChar || key == kEscapeKeyCode || key == kClearKeyCode) {
        return true;
    }
    if (cmd && c == '.') {
        return true;
    }
    if (cmd && (c == 'b' || c == 'B')) {
        return true;
    }
    return false;
}

static Boolean handle_key(EventRecord *ev) {
    char c = (char)(ev->message & charCodeMask);
    Boolean cmd = (ev->modifiers & cmdKey) != 0;

    note_user_input();

    if (is_back_key(ev, c, cmd)) {
        if (gReactPrompt) {
            cancel_react_prompt();
            return true;
        }
        if (gInThread) {
            exit_thread();
            return true;
        }
        if (c == kEscapeChar || ((ev->message & keyCodeMask) >> 8) == kEscapeKeyCode
            || ((ev->message & keyCodeMask) >> 8) == kClearKeyCode
            || (cmd && c == '.')) {
            return true;
        }
        return false;
    }
    if (cmd && (c == 'c' || c == 'C')) {
        load_channels();
        return true;
    }
    if (cmd && (c == 'r' || c == 'R')) {
        gViewOff = 0;
        fetch_view(true);
        return true;
    }
    if (cmd && (c == 't' || c == 'T')) {
        gShowTimestamps = !gShowTimestamps;
        set_status(gShowTimestamps ? "Timestamps on (Cmd-T)" : "Timestamps off (Cmd-T)");
        return true;
    }
    if (cmd && (c == 'e' || c == 'E')) {
        begin_react_prompt();
        return true;
    }
    if (cmd && (c == ']' || c == kDownArrowChar)) {
        return scroll_messages(1);
    }
    if (cmd && (c == '[' || c == kUpArrowChar)) {
        return scroll_messages(-1);
    }
    if (c == '\r' || c == '\n') {
        if (gReactPrompt) {
            submit_react_prompt();
        } else {
            post_message();
        }
        return true;
    }
    if (gComposeTE) {
        TEKey(c, gComposeTE);
    }
    return false;
}

static void drain_keys(void) {
    EventRecord ev;
    Boolean need_draw = false;

    while (EventAvail(keyDownMask | autoKeyMask, &ev)) {
        if (!GetNextEvent(keyDownMask | autoKeyMask, &ev)) {
            break;
        }
        if (ev.what != keyDown) {
            continue;
        }
        if (handle_key(&ev)) {
            need_draw = true;
        }
    }
    if (need_draw) {
        draw_ui();
    }
}

int main(void) {
    Boolean done = false;

    init_app();

    while (!done && !gHarnessQuit) {
        EventRecord ev;
        if (WaitNextEvent(everyEvent, &ev, 2, NULL)) {
            switch (ev.what) {
            case updateEvt:
                BeginUpdate(gWin);
                draw_ui();
                EndUpdate(gWin);
                break;
            case mouseDown: {
                WindowPtr w;
                short part = FindWindow(ev.where, &w);
                if (part == inGoAway && w == gWin) {
                    done = true;
                } else if (part == inDrag && w == gWin) {
                    DragWindow(w, ev.where, &qd.screenBits.bounds);
                } else if (part == inContent && w == gWin) {
                    SelectWindow(w);
                    do_click(ev.where);
                }
                break;
            }
            case keyDown:
                if (handle_key(&ev)) {
                    draw_ui();
                }
                drain_keys();
                break;
            case autoKey:
                break;
            case kHighLevelEvent:
                HarnessProcessAppleEvent(&ev);
                break;
            default:
                break;
            }
        } else {
            if (gComposeTE) {
                TEIdle(gComposeTE);
            }
            maybe_auto_refresh();
        }
    }

    RHTTPClose();
    if (gComposeTE) {
        TEDispose(gComposeTE);
        gComposeTE = NULL;
    }
    if (gWin) {
        DisposeWindow(gWin);
        gWin = NULL;
    }
    HarnessReturnToLoader();
    return 0;
}
